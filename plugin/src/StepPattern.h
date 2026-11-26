#pragma once
// Which step of a sequence (or which random slices) play at each tick of a chase.
// A tick is one step of playback; ticks count on across loops of the Step parameter,
// so random orders differ from loop to loop yet stay the same whenever the same tick is drawn
// again (echoes look back at earlier ticks and must see what was lit then).
// Plain C++ with no GL, so the tests can check it.
#include <algorithm>
#include <cstdint>
#include <vector>

namespace stilletchaser
{
enum Direction
{
	DIRECTION_FORWARD,
	DIRECTION_BACKWARD,
	DIRECTION_PING_PONG,
	DIRECTION_RANDOM,
};
static const int DIRECTIONS = 4;

inline int64_t FloorDiv( int64_t a, int64_t b )
{
	int64_t q = a / b;
	return ( a % b != 0 && ( ( a < 0 ) != ( b < 0 ) ) ) ? q - 1 : q;
}

inline int64_t FloorMod( int64_t a, int64_t b )
{
	return a - FloorDiv( a, b ) * b;
}

inline uint32_t HashTick( uint64_t x )
{
	// splitmix64, folded to 32 bits.
	x += 0x9E3779B97F4A7C15ull;
	x = ( x ^ ( x >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
	x = ( x ^ ( x >> 27 ) ) * 0x94D049BB133111EBull;
	x ^= x >> 31;
	return (uint32_t)( x ^ ( x >> 32 ) );
}

// 0..n-1 shuffled, the same for the same deck and salt. Consecutive decks never repeat across their seam:
// if this deck would start with the previous deck's last entry, its first two entries swap.
inline std::vector< int > Deck( int n, int64_t deck, uint32_t salt )
{
	std::vector< int > order( (size_t)std::max( n, 0 ) );
	for( int i = 0; i < n; ++i )
		order[ (size_t)i ] = i;
	if( n <= 1 )
		return order;
	if( n == 2 )
		return order;// Two entries without repeats can only alternate.
	auto shuffled = [ & ]( int64_t which ) {
		std::vector< int > o( order );
		uint32_t state = HashTick( (uint64_t)which ^ ( (uint64_t)salt << 32 ) );
		for( int i = n - 1; i > 0; --i )
		{
			state = HashTick( state + (uint64_t)i );
			std::swap( o[ (size_t)i ], o[ state % (uint32_t)( i + 1 ) ] );
		}
		return o;
	};
	std::vector< int > current = shuffled( deck );
	// Swapping the first two never moves the last entry (n >= 3), so the previous deck's last is final.
	if( current.front() == shuffled( deck - 1 ).back() )
		std::swap( current[ 0 ], current[ 1 ] );
	return current;
}

// Ticks in one run of Step 0..1. Ping-pong goes there and back in one run, without repeating the ends.
inline int TicksPerCycle( int direction, int steps )
{
	if( steps <= 0 )
		return 0;
	if( direction == DIRECTION_PING_PONG && steps > 1 )
		return 2 * steps - 2;
	return steps;
}

// The sequence step played at `tick` (any integer, negative included).
inline int StepAt( int direction, int steps, int64_t tick )
{
	if( steps <= 1 )
		return 0;
	switch( direction )
	{
	case DIRECTION_BACKWARD:
		return steps - 1 - (int)FloorMod( tick, steps );
	case DIRECTION_PING_PONG:
	{
		const int64_t period = 2 * steps - 2;
		const int p          = (int)FloorMod( tick, period );
		return p < steps ? p : (int)( period - p );
	}
	case DIRECTION_RANDOM:
	{
		if( steps == 2 )
			return (int)FloorMod( tick, 2 );
		const auto deck = Deck( steps, FloorDiv( tick, steps ), 0x5EC0u );
		return deck[ (size_t)FloorMod( tick, steps ) ];
	}
	default:
		return (int)FloorMod( tick, steps );
	}
}

// The `perTick` slices (indices into `eligible`) lit at `tick` by the Random driver: dealt from shuffled
// decks of all eligible slices, so every slice comes up before any comes up again.
inline std::vector< int > RandomSlicesAt( const std::vector< int >& eligible, int perTick, int64_t tick )
{
	std::vector< int > lit;
	const int n = (int)eligible.size();
	if( n == 0 || perTick <= 0 )
		return lit;
	const int k = std::min( perTick, n );
	int64_t cachedDeck = INT64_MIN;
	std::vector< int > deck;
	for( int j = 0; j < k; ++j )
	{
		const int64_t card  = tick * k + j;
		const int64_t which = FloorDiv( card, n );
		if( which != cachedDeck )
		{
			cachedDeck = which;
			deck = Deck( n, which, 0x5A1Cu );
		}
		const int slice = eligible[ (size_t)deck[ (size_t)FloorMod( card, n ) ] ];
		if( std::find( lit.begin(), lit.end(), slice ) == lit.end() )
			lit.push_back( slice );
	}
	return lit;
}
} // namespace stilletchaser
