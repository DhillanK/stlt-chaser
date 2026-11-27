// Checks the document loader against files the editor wrote:
//   document_test <current.stchase> <envelopes.json>
#include <cstdio>
#include <cmath>
#include "ChaseDocument.h"
#include "StepPattern.h"
#include <set>

using namespace stilletchaser;

static int failures = 0;
static void CheckStepPatterns();
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

	CheckStepPatterns();
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

#undef CHECK_SEQ
static void CheckStepPatterns()
{
	auto run = []( int direction, int steps, int from, int to ) {
		std::vector< int > out;
		for( int t = from; t < to; ++t )
			out.push_back( StepAt( direction, steps, t ) );
		return out;
	};
	CHECK( ( run( DIRECTION_FORWARD, 4, 0, 6 ) == std::vector< int >{ 0, 1, 2, 3, 0, 1 } ) );
	CHECK( ( run( DIRECTION_FORWARD, 4, -2, 0 ) == std::vector< int >{ 2, 3 } ) );// echoes before the first tick
	CHECK( ( run( DIRECTION_BACKWARD, 4, 0, 6 ) == std::vector< int >{ 3, 2, 1, 0, 3, 2 } ) );
	CHECK( ( run( DIRECTION_PING_PONG, 4, 0, 8 ) == std::vector< int >{ 0, 1, 2, 3, 2, 1, 0, 1 } ) );
	CHECK( TicksPerCycle( DIRECTION_PING_PONG, 4 ) == 6 && TicksPerCycle( DIRECTION_FORWARD, 4 ) == 4 );
	CHECK( TicksPerCycle( DIRECTION_PING_PONG, 1 ) == 1 && TicksPerCycle( DIRECTION_RANDOM, 0 ) == 0 );
	CHECK( ( run( DIRECTION_PING_PONG, 1, 0, 3 ) == std::vector< int >{ 0, 0, 0 } ) );

	// Random order: each loop plays every step once, no step twice in a row (seams included), and the
	// same tick always gives the same step.
	for( int steps : { 2, 3, 5, 8, 13 } )
	{
		int previous = -1;
		for( int loop = -3; loop < 40; ++loop )
		{
			std::set< int > seen;
			for( int t = 0; t < steps; ++t )
			{
				const int s = StepAt( DIRECTION_RANDOM, steps, (int64_t)loop * steps + t );
				CHECK( s >= 0 && s < steps );
				CHECK( s != previous );
				seen.insert( s );
				previous = s;
			}
			CHECK( (int)seen.size() == steps );
		}
		CHECK( StepAt( DIRECTION_RANDOM, steps, 77 ) == StepAt( DIRECTION_RANDOM, steps, 77 ) );
	}
	// ...and loops are not all the same order.
	CHECK( run( DIRECTION_RANDOM, 8, 0, 8 ) != run( DIRECTION_RANDOM, 8, 8, 16 ) || run( DIRECTION_RANDOM, 8, 8, 16 ) != run( DIRECTION_RANDOM, 8, 16, 24 ) );

	// Random driver: only eligible slices, `perTick` distinct ones, and with one per tick every slice comes up
	// before any repeats, never twice in a row.
	const std::vector< int > eligible{ 2, 3, 5, 7, 11, 13, 17, 19 };
	int previous = -1;
	for( int deck = 0; deck < 30; ++deck )
	{
		std::set< int > seen;
		for( int t = 0; t < (int)eligible.size(); ++t )
		{
			const auto lit = RandomSlicesAt( eligible, 1, (int64_t)deck * (int64_t)eligible.size() + t );
			CHECK( lit.size() == 1 );
			CHECK( std::find( eligible.begin(), eligible.end(), lit[ 0 ] ) != eligible.end() );
			CHECK( lit[ 0 ] != previous );
			previous = lit[ 0 ];
			seen.insert( lit[ 0 ] );
		}
		CHECK( seen.size() == eligible.size() );
	}
	for( int t = -5; t < 50; ++t )
	{
		const auto three = RandomSlicesAt( eligible, 3, t );
		CHECK( three.size() >= 2 && three.size() <= 3 );// a deck seam can deal the same slice twice in one tick
		CHECK( RandomSlicesAt( eligible, 3, t ) == three );
	}
	CHECK( RandomSlicesAt( eligible, 20, 4 ).size() == eligible.size() );// more than there are: all of them
	CHECK( RandomSlicesAt( {}, 2, 0 ).empty() );
	const std::vector< int > pair{ 4, 9 };
	for( int t = 0; t < 10; ++t )
		CHECK( RandomSlicesAt( pair, 1, t )[ 0 ] != RandomSlicesAt( pair, 1, t + 1 )[ 0 ] );
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
