// Checks the document loader against files the editor wrote:
//   document_test <current.stchase> <envelopes.json>
#include <cstdio>
#include <cmath>
#include "ChaseDocument.h"

using namespace stilletchaser;

static int failures = 0;
static void CheckDirections();
#define CHECK( cond )                                                    \
	do                                                                   \
	{                                                                    \
		if( !( cond ) )                                                  \
		{                                                                \
			std::printf( "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond ); \
			++failures;                                                  \
		}                                                                \
	} while( 0 )

int main( int argc, char** argv )
{
	CHECK( SustainRelease( 0.2f, 0.8f, 0.1f ) == 1.f );
	CHECK( std::fabs( SustainRelease( 0.2f, 0.8f, 0.5f ) - 0.5f ) < 1e-5f );
	CHECK( SustainRelease( 0.2f, 0.8f, 0.9f ) == 0.f );
	CHECK( SustainRelease( 1.f, 1.f, 0.99f ) == 1.f );
	std::array< float, 256 > ramp;
	for( int i = 0; i < 256; ++i )
		ramp[ i ] = i / 255.f;
	CHECK( std::fabs( SampleLut( ramp, 0.5f ) - 0.5f ) < 1e-4f );

	CheckDirections();

	auto missing = LoadChaseDocument( "does-not-exist.stchase", "nope.json" );
	CHECK( !missing->error.empty() && missing->slices.empty() );

	if( argc < 3 )
	{
		std::printf( "%s\n", failures ? "FAILED" : "ok (no document given)" );
		return failures;
	}
	auto doc = LoadChaseDocument( argv[ 1 ], argv[ 2 ] );
	std::printf( "error: '%s'\ncomp %.0fx%.0f, %zu slices, %zu sequences, %zu views, %zu envelopes\n", doc->error.c_str(), doc->compWidth, doc->compHeight,
				 doc->slices.size(), doc->sequences.size(), doc->views.size(), doc->envelopes.size() );
	CHECK( doc->error.empty() );
	CHECK( !doc->slices.empty() );
	CHECK( doc->views.size() >= 1 && doc->views[ 0 ].name == "All" );
	for( const Slice& s : doc->slices )
	{
		CHECK( s.indices.size() == ( s.points.size() - 2 ) * 3 );
		std::printf( "  %-10s %zu pts, frame (%.3f,%.3f) x(%.3f,%.3f) y(%.3f,%.3f)\n", s.name.c_str(), s.points.size(), s.origin.x, s.origin.y, s.axisX.x,
					 s.axisX.y, s.axisY.x, s.axisY.y );
	}
	for( const Sequence& q : doc->sequences )
	{
		std::printf( "  %s: %zu steps, first step lights %zu\n", q.name.c_str(), q.steps.size(), q.steps[ 0 ].size() );
	}
	std::printf( "%s\n", failures ? "FAILED" : "ok" );
	return failures;
}

// Per-slice directions: read from the document, unknown values and unlisted slices are Auto.
static void CheckDirections()
{
	const char* path = "directions_test.stchase";
	if( FILE* f = std::fopen( path, "w" ) )
	{
		std::fputs( R"({"format":"stilletchaser","version":1,"source":{"compWidth":100,"compHeight":100},
			"slices":[{"id":"a","points":[[0,0],[10,0],[10,10],[0,10]]},{"id":"b","points":[[20,0],[30,0],[30,10],[20,10]]},
			          {"id":"c","points":[[40,0],[50,0],[50,10],[40,10]]}],
			"sequences":[{"id":"s","name":"S","steps":[["a"]]}],"views":[{"id":"all","name":"All","hidden":[]}],
			"directions":{"a":{"wipe":"mirror-v","outline":"ccw"},"b":{"wipe":"sideways"},"c":{"wipe":null,"outline":7},"gone":{"wipe":"up"},"bad":3}})",
					f );
		std::fclose( f );
	}
	auto doc = LoadChaseDocument( path, "nope.json" );
	std::remove( path );
	CHECK( doc->error.empty() && doc->slices.size() == 3 );
	if( doc->slices.size() == 3 )
	{
		CHECK( doc->slices[ 0 ].wipe == WIPE_MIRROR_V && doc->slices[ 0 ].outline == OUTLINE_CCW );
		CHECK( doc->slices[ 1 ].wipe == WIPE_AUTO && doc->slices[ 1 ].outline == OUTLINE_AUTO );
		CHECK( doc->slices[ 2 ].wipe == WIPE_AUTO && doc->slices[ 2 ].outline == OUTLINE_AUTO );// null and a number: Auto, document kept
	}
	const WipeDirection automatic{ true, true, false };
	auto same = []( WipeDirection a, WipeDirection b ) { return a.alongY == b.alongY && a.reversed == b.reversed && a.mirror == b.mirror; };
	CHECK( same( ResolveWipe( WIPE_AUTO, automatic ), automatic ) );
	CHECK( same( ResolveWipe( WIPE_UP, automatic ), WipeDirection{ true, true, false } ) );
	CHECK( same( ResolveWipe( WIPE_DOWN, automatic ), WipeDirection{ true, false, false } ) );
	CHECK( same( ResolveWipe( WIPE_LEFT, automatic ), WipeDirection{ false, true, false } ) );
	CHECK( same( ResolveWipe( WIPE_RIGHT, automatic ), WipeDirection{ false, false, false } ) );
	CHECK( same( ResolveWipe( WIPE_MIRROR_V, automatic ), WipeDirection{ true, false, true } ) );
	CHECK( same( ResolveWipe( WIPE_MIRROR_H, automatic ), WipeDirection{ false, false, true } ) );
}
