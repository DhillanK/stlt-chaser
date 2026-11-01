#include "ChaseDocument.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include "json.hpp"
#if defined( _WIN32 )
#include <windows.h>
#endif

namespace stilletchaser
{
namespace
{
using json = nlohmann::json;

bool ReadFile( const std::string& path, std::string& out )
{
	std::ifstream file( std::filesystem::u8path( path ), std::ios::binary );
	if( !file )
		return false;
	std::stringstream buffer;
	buffer << file.rdbuf();
	out = buffer.str();
	return true;
}

int64_t Stamp( const std::string& path )
{
	std::error_code ec;
	auto time = std::filesystem::last_write_time( std::filesystem::u8path( path ), ec );
	return ec ? -1 : (int64_t)time.time_since_epoch().count();
}

float Cross( Vec2 o, Vec2 a, Vec2 b )
{
	return ( a.x - o.x ) * ( b.y - o.y ) - ( a.y - o.y ) * ( b.x - o.x );
}

bool InTriangle( Vec2 p, Vec2 a, Vec2 b, Vec2 c )
{
	float d1 = Cross( a, b, p ), d2 = Cross( b, c, p ), d3 = Cross( c, a, p );
	bool neg = d1 < 0 || d2 < 0 || d3 < 0;
	bool pos = d1 > 0 || d2 > 0 || d3 > 0;
	return !( neg && pos );
}

// Ear clipping: slices are small polygons, so the simple O(n^3) version is plenty.
std::vector< uint16_t > Triangulate( const std::vector< Vec2 >& pts )
{
	std::vector< uint16_t > result;
	std::vector< uint16_t > remaining( pts.size() );
	for( size_t i = 0; i < pts.size(); ++i )
		remaining[ i ] = (uint16_t)i;

	float area = 0.f;
	for( size_t i = 0; i < pts.size(); ++i )
	{
		const Vec2& a = pts[ i ];
		const Vec2& b = pts[ ( i + 1 ) % pts.size() ];
		area += a.x * b.y - b.x * a.y;
	}
	const float winding = area >= 0.f ? 1.f : -1.f;

	size_t guard = 0;
	while( remaining.size() > 3 && guard++ < pts.size() * pts.size() )
	{
		bool clipped = false;
		for( size_t i = 0; i < remaining.size(); ++i )
		{
			uint16_t ia = remaining[ ( i + remaining.size() - 1 ) % remaining.size() ];
			uint16_t ib = remaining[ i ];
			uint16_t ic = remaining[ ( i + 1 ) % remaining.size() ];
			if( Cross( pts[ ia ], pts[ ib ], pts[ ic ] ) * winding <= 0.f )
				continue;
			bool ear = true;
			for( uint16_t j : remaining )
			{
				if( j != ia && j != ib && j != ic && InTriangle( pts[ j ], pts[ ia ], pts[ ib ], pts[ ic ] ) )
				{
					ear = false;
					break;
				}
			}
			if( !ear )
				continue;
			result.insert( result.end(), { ia, ib, ic } );
			remaining.erase( remaining.begin() + i );
			clipped = true;
			break;
		}
		if( !clipped )
			break;// Degenerate polygon: fan the rest rather than drop it.
	}
	for( size_t i = 1; i + 1 < remaining.size(); ++i )
		result.insert( result.end(), { remaining[ 0 ], remaining[ i ], remaining[ i + 1 ] } );
	return result;
}

void SetFrame( Slice& slice )
{
	const auto& p = slice.points;
	if( p.size() == 4 )
	{
		slice.origin = p[ 0 ];
		slice.axisX  = { p[ 1 ].x - p[ 0 ].x, p[ 1 ].y - p[ 0 ].y };
		slice.axisY  = { p[ 3 ].x - p[ 0 ].x, p[ 3 ].y - p[ 0 ].y };
		return;
	}
	float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
	for( const Vec2& v : p )
	{
		minX = std::min( minX, v.x );
		minY = std::min( minY, v.y );
		maxX = std::max( maxX, v.x );
		maxY = std::max( maxY, v.y );
	}
	slice.origin = { minX, minY };
	slice.axisX  = { maxX - minX, 0.f };
	slice.axisY  = { 0.f, maxY - minY };
}

void ParseDocument( const json& j, ChaseDocument& doc )
{
	if( j.value( "format", "" ) != "stilletchaser" )
		throw std::runtime_error( "not a Stilletchaser document" );
	doc.compWidth  = j.at( "source" ).value( "compWidth", 0.f );
	doc.compHeight = j.at( "source" ).value( "compHeight", 0.f );
	if( doc.compWidth <= 0.f || doc.compHeight <= 0.f )
		throw std::runtime_error( "document has no composition size" );

	std::unordered_map< std::string, int > indexOf;
	for( const json& s : j.at( "slices" ) )
	{
		Slice slice;
		slice.id   = s.value( "id", "" );
		slice.name = s.value( "name", "" );
		for( const json& p : s.at( "points" ) )
			slice.points.push_back( { p.at( 0 ).get< float >() / doc.compWidth, p.at( 1 ).get< float >() / doc.compHeight } );
		if( slice.points.size() < 3 || slice.points.size() > 4096 )
			continue;
		slice.indices = Triangulate( slice.points );
		SetFrame( slice );
		indexOf[ slice.id ] = (int)doc.slices.size();
		doc.slices.push_back( std::move( slice ) );
	}

	for( const json& s : j.at( "sequences" ) )
	{
		Sequence sequence;
		sequence.id   = s.value( "id", "" );
		sequence.name = s.value( "name", "" );
		for( const json& step : s.at( "steps" ) )
		{
			std::vector< int > lit;
			for( const json& id : step )
			{
				auto found = indexOf.find( id.get< std::string >() );
				if( found != indexOf.end() )
					lit.push_back( found->second );
			}
			sequence.steps.push_back( std::move( lit ) );
		}
		if( sequence.steps.empty() )
			sequence.steps.emplace_back();
		doc.sequences.push_back( std::move( sequence ) );
	}

	View all{ "all", "All", std::vector< bool >( doc.slices.size(), true ) };
	doc.views.push_back( all );
	for( const json& v : j.at( "views" ) )
	{
		if( v.value( "id", "" ) == "all" || !v.contains( "visible" ) )
			continue;
		View view{ v.value( "id", "" ), v.value( "name", "" ), std::vector< bool >( doc.slices.size(), false ) };
		for( const json& id : v.at( "visible" ) )
		{
			auto found = indexOf.find( id.get< std::string >() );
			if( found != indexOf.end() )
				view.includes[ found->second ] = true;
		}
		doc.views.push_back( std::move( view ) );
	}
}

void ParseEnvelopes( const json& j, ChaseDocument& doc )
{
	for( const json& e : j.at( "envelopes" ) )
	{
		const json& lut = e.at( "lut" );
		if( lut.size() != 256 )
			continue;
		Envelope envelope;
		envelope.id   = e.value( "id", "" );
		envelope.name = e.value( "name", "" );
		for( size_t i = 0; i < 256; ++i )
			envelope.lut[ i ] = std::clamp( lut[ i ].get< float >(), 0.f, 1.f );
		doc.envelopes.push_back( std::move( envelope ) );
	}
}
}// namespace

// Where the editor keeps its working copy: Electron's appData folder for the app, or STLT_DATA_DIR when the
// host sets it (STLT-VJ does, so its sequences stay apart from a Resolume setup's).
std::string DataDirectory()
{
#if defined( _WIN32 )
	// Read from the process environment itself: a host that sets the variable while running may use another C runtime.
	wchar_t custom[ 4096 ];
	DWORD length = GetEnvironmentVariableW( L"STLT_DATA_DIR", custom, 4096 );
	if( length > 0 && length < 4096 )
	{
		int size = WideCharToMultiByte( CP_UTF8, 0, custom, (int)length, nullptr, 0, nullptr, nullptr );
		std::string utf8( (size_t)size, '\0' );
		WideCharToMultiByte( CP_UTF8, 0, custom, (int)length, utf8.data(), size, nullptr, nullptr );
		return utf8;
	}
#else
	if( const char* custom = std::getenv( "STLT_DATA_DIR" ); custom && *custom )
		return custom;
#endif
#if defined( _WIN32 )
	const char* appData = std::getenv( "APPDATA" );
	return std::string( appData ? appData : "." ) + "\\Stilletchaser";
#elif defined( __APPLE__ )
	const char* home = std::getenv( "HOME" );
	return std::string( home ? home : "." ) + "/Library/Application Support/Stilletchaser";
#else
	const char* config = std::getenv( "XDG_CONFIG_HOME" );
	const char* home   = std::getenv( "HOME" );
	return config ? std::string( config ) + "/Stilletchaser" : std::string( home ? home : "." ) + "/.config/Stilletchaser";
#endif
}

std::shared_ptr< const ChaseDocument > LoadChaseDocument( const std::string& documentPath, const std::string& envelopesPath )
{
	auto doc = std::make_shared< ChaseDocument >();
	std::string text;
	if( !ReadFile( documentPath, text ) )
	{
		doc->error = "No Stilletchaser document yet: open the Stilletchaser editor";
	}
	else
	{
		try
		{
			ParseDocument( json::parse( text ), *doc );
		}
		catch( const std::exception& e )
		{
			*doc       = ChaseDocument();
			doc->error = std::string( "Cannot read " ) + documentPath + ": " + e.what();
		}
	}
	if( ReadFile( envelopesPath, text ) )
	{
		try
		{
			ParseEnvelopes( json::parse( text ), *doc );
		}
		catch( const std::exception& )
		{
			doc->envelopes.clear();
		}
	}
	return doc;
}

DocumentWatcher& DocumentWatcher::Instance()
{
	static DocumentWatcher watcher;
	return watcher;
}

DocumentWatcher::DocumentWatcher() :
	documentPath( ( std::filesystem::u8path( DataDirectory() ) / "current.stchase" ).u8string() ),
	envelopesPath( ( std::filesystem::u8path( DataDirectory() ) / "envelopes.json" ).u8string() )
{
	Reload();
}

void DocumentWatcher::Reload()
{
	documentStamp  = Stamp( documentPath );
	envelopesStamp = Stamp( envelopesPath );
	document       = LoadChaseDocument( documentPath, envelopesPath );
	++version;
}

std::shared_ptr< const ChaseDocument > DocumentWatcher::Current()
{
	// Resolume may render layers on different threads; keep the check and swap atomic.
	static std::mutex mutex;
	std::lock_guard< std::mutex > lock( mutex );
	uint64_t now = (uint64_t)std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now().time_since_epoch() ).count();
	if( now - lastCheck >= 500 )
	{
		lastCheck = now;
		if( Stamp( documentPath ) != documentStamp || Stamp( envelopesPath ) != envelopesStamp )
			Reload();
	}
	return document;
}

float SampleLut( const std::array< float, 256 >& lut, float x )
{
	float pos = std::clamp( x, 0.f, 1.f ) * 255.f;
	int i     = std::min( (int)pos, 254 );
	float f   = pos - (float)i;
	return lut[ i ] * ( 1.f - f ) + lut[ i + 1 ] * f;
}

// Full opacity until `sustain`, a linear fade until `release`, then off (x is 0..1 of the step's life).
float SustainRelease( float sustain, float release, float x )
{
	if( x < sustain )
		return 1.f;
	if( x >= release )
		return 0.f;
	return 1.f - ( x - sustain ) / ( release - sustain );
}
}// namespace stilletchaser
