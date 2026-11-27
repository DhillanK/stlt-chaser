#pragma once
#include <FFGLSDK.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "ChaseDocument.h"

// Helpers shared by the Stilletchaser plugins.
namespace stilletchaser
{
enum Scaling
{
	SCALING_MASK,
	SCALING_FILL,
	SCALING_STRETCH,
};

// An option parameter's value as an element index.
inline int Option( float value, int count )
{
	return std::clamp( (int)std::lround( value ), 0, std::max( 0, count - 1 ) );
}

inline std::vector< float > Indices( size_t count )
{
	std::vector< float > values( count );
	for( size_t i = 0; i < count; ++i )
		values[ i ] = (float)i;
	return values;
}

inline std::vector< std::string > SequenceNames( const ChaseDocument& doc )
{
	std::vector< std::string > names;
	for( const Sequence& s : doc.sequences )
		names.push_back( s.name );
	if( names.empty() )
		names.push_back( "No sequences: open the Stilletchaser editor" );
	return names;
}

inline std::vector< std::string > ViewNames( const ChaseDocument& doc )
{
	std::vector< std::string > names;
	for( const View& v : doc.views )
		names.push_back( v.name );
	if( names.empty() )
		names.push_back( "All" );
	return names;
}

// "firstChoice" followed by the envelopes drawn in the editor.
inline std::vector< std::string > EnvelopeNames( const ChaseDocument& doc, const char* firstChoice )
{
	std::vector< std::string > names{ firstChoice };
	for( const Envelope& e : doc.envelopes )
		names.push_back( e.name );
	return names;
}

// Option value 0 = no envelope, 1.. = the editor's envelopes.
inline const Envelope* ChosenEnvelope( const ChaseDocument& doc, float value )
{
	int index = (int)std::lround( value ) - 1;
	return index >= 0 && index < (int)doc.envelopes.size() ? &doc.envelopes[ index ] : nullptr;
}

// Position of a point in the slice's own frame: 0..1 along its x edge and its y edge
// (y runs down the slice, like the composition).
inline Vec2 SliceLocal( const Slice& slice, Vec2 p )
{
	float det = slice.axisX.x * slice.axisY.y - slice.axisX.y * slice.axisY.x;
	if( std::fabs( det ) < 1e-9f )
		return { 0.f, 0.f };
	float dx = p.x - slice.origin.x, dy = p.y - slice.origin.y;
	return { ( dx * slice.axisY.y - dy * slice.axisY.x ) / det, ( slice.axisX.x * dy - slice.axisX.y * dx ) / det };
}

// Slice size in composition pixels along its own edges.
inline Vec2 SliceSize( const ChaseDocument& doc, const Slice& slice )
{
	return { std::hypot( slice.axisX.x * doc.compWidth, slice.axisX.y * doc.compHeight ),
			 std::hypot( slice.axisY.x * doc.compWidth, slice.axisY.y * doc.compHeight ) };
}

inline Vec2 SliceCentre( const Slice& slice )
{
	return { slice.origin.x + ( slice.axisX.x + slice.axisY.x ) * 0.5f, slice.origin.y + ( slice.axisX.y + slice.axisY.y ) * 0.5f };
}

// Texture coordinate (0..1, y up) of the content shown at point `p` of a slice.
// Mask: the content under the slice. Stretch: the whole content squeezed into the slice.
// Fill: the content cropped to the slice's shape, centred.
inline Vec2 ContentUV( const ChaseDocument& doc, const Slice& slice, Vec2 p, int scaling, float contentAspect )
{
	const float det = slice.axisX.x * slice.axisY.y - slice.axisX.y * slice.axisY.x;
	if( scaling == SCALING_MASK || std::fabs( det ) < 1e-9f )
		return { p.x, 1.f - p.y };
	const Vec2 local = SliceLocal( slice, p );
	float cropX = 1.f, cropY = 1.f;
	if( scaling == SCALING_FILL )
	{
		Vec2 size         = SliceSize( doc, slice );
		float sliceAspect = size.y > 0.f ? size.x / size.y : 1.f;
		if( sliceAspect > contentAspect )
			cropY = contentAspect / sliceAspect;
		else
			cropX = sliceAspect / contentAspect;
	}
	return { 0.5f + ( local.x - 0.5f ) * cropX, 1.f - ( 0.5f + ( local.y - 0.5f ) * cropY ) };
}

// ---- Wipes (STLT-WIPER and the Wiper module of STLT-CHASER) ----

const int MAX_WIPE_LOOPS = 16;

// A wipe running through the slices of one step at this moment.
struct ActiveWipe
{
	int stepIndex;
	float head;// Leading edge position along the slice, 0..1 + length.
	bool odd;  // Odd step with Odd/Even on: runs the other way.
};

// Step (0..1) runs through the sequence `loops` times. Each step starts a wipe; with overlap a wipe
// lasts longer than one step, so the next one starts before it ends. Wraps around the end of the
// timeline so a looped Step plays seamlessly.
inline std::vector< ActiveWipe > ActiveWipes( int count, float step, int loops, float length, float overlap, bool oddEven )
{
	std::vector< ActiveWipe > wipes;
	if( count <= 0 )
		return wipes;
	loops                = std::clamp( loops, 1, MAX_WIPE_LOOPS );
	const int total      = count * loops;
	const float duration = 1.f / ( 1.f - std::clamp( overlap, 0.f, 0.95f ) );
	const float position = std::min( step, 0.99999f ) * (float)total;
	for( int j = (int)std::floor( position - duration ); j <= (int)std::floor( position ); ++j )
	{
		float t = ( position - (float)j ) / duration;
		if( t < 0.f || t >= 1.f )
			continue;
		const int stepIndex = ( ( j % total ) + total ) % total % count;
		wipes.push_back( { stepIndex, t * ( 1.f + length ), oddEven && stepIndex % 2 == 1 } );
	}
	return wipes;
}

// Automatic direction: vertical slices wipe bottom to top, horizontal ones away from the centre
// of the stage. Swap treats vertical slices as horizontal and the other way round.

inline WipeDirection BaseWipeDirection( const ChaseDocument& doc, const Slice& slice, bool swap )
{
	const Vec2 size = SliceSize( doc, slice );
	bool vertical   = size.y > size.x;
	if( swap )
		vertical = !vertical;
	return { vertical, vertical ? true : SliceCentre( slice ).x < 0.5f };
}

// The direction set on the slice in the editor; Auto slices follow the rule above (and Swap).
inline WipeDirection SliceWipeDirection( const ChaseDocument& doc, const Slice& slice, bool swap )
{
	return ResolveWipe( slice.wipe, BaseWipeDirection( doc, slice, swap ) );
}

// GLSL: opacity of a wipe band at slice-local position `local` (0 = nothing).
inline const char* WipeAlphaGLSL()
{
	return R"(
uniform float wipeHead;
uniform float wipeLength;
uniform int wipeAlongY;
uniform int wipeReversed;
uniform int wipeMirror;
uniform int wipeUseCurve;
uniform float wipeCurve[ 256 ];
float wipeAlpha( vec2 local )
{
	float d = wipeAlongY != 0 ? local.y : local.x;
	if( wipeMirror != 0 )
		d = abs( d - 0.5 ) * 2.0;// from the middle out
	if( wipeReversed != 0 )
		d = 1.0 - d;
	// 0 at the leading edge, 1 at the tail of the band.
	float x = ( wipeHead - d ) / wipeLength;
	if( x < 0.0 || x > 1.0 )
		return 0.0;
	return wipeUseCurve != 0 ? wipeCurve[ int( x * 255.0 ) ] : 1.0 - x;
}
)";
}

// Clip-space vertex shader shared by the slice renderers: composition positions in, y flipped.
inline const char* CompositionVertexHeader()
{
	return R"(#version 410 core
vec4 compToClip( vec2 compPos )
{
	return vec4( compPos.x * 2.0 - 1.0, 1.0 - compPos.y * 2.0, 0.0, 1.0 );
}
)";
}

// Base for the Stilletchaser plugins.
class StilletchaserPlugin : public CFFGLPlugin
{
protected:
	// A parameter with its own range. The host sends values in that range, but the SDK clamps
	// the default of a standard parameter to 0..1, so put the real default back.
	void SetRangedParamInfo( unsigned int id, const char* name, unsigned int type, float defaultValue, float min, float max )
	{
		SetParamInfo( id, name, type, defaultValue );
		if( ParamInfo* info = FindParamInfo( id ) )
			info->defaultFloatVal = defaultValue;
		SetParamRange( id, min, max );
	}
};

// A vertex array whose attributes are consecutive floats, `sizes` per attribute.
class FloatVertexBuffer
{
public:
	bool Init( std::vector< int > sizes );
	void Release();
	void Upload( const std::vector< float >& data );
	GLuint VAO() const
	{
		return vao;
	}
	int FloatsPerVertex() const
	{
		return stride;
	}

private:
	GLuint vao = 0, vbo = 0;
	int stride = 0;
};

inline bool FloatVertexBuffer::Init( std::vector< int > sizes )
{
	stride = 0;
	for( int s : sizes )
		stride += s;
	glGenVertexArrays( 1, &vao );
	glGenBuffers( 1, &vbo );
	glBindVertexArray( vao );
	glBindBuffer( GL_ARRAY_BUFFER, vbo );
	int offset = 0;
	for( size_t i = 0; i < sizes.size(); ++i )
	{
		glEnableVertexAttribArray( (GLuint)i );
		glVertexAttribPointer( (GLuint)i, sizes[ i ], GL_FLOAT, GL_FALSE, stride * sizeof( float ), (void*)( offset * sizeof( float ) ) );
		offset += sizes[ i ];
	}
	glBindVertexArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	return vao != 0 && vbo != 0;
}

inline void FloatVertexBuffer::Release()
{
	if( vbo )
		glDeleteBuffers( 1, &vbo );
	if( vao )
		glDeleteVertexArrays( 1, &vao );
	vbo = vao = 0;
}

inline void FloatVertexBuffer::Upload( const std::vector< float >& data )
{
	glBindBuffer( GL_ARRAY_BUFFER, vbo );
	glBufferData( GL_ARRAY_BUFFER, data.size() * sizeof( float ), data.data(), GL_STREAM_DRAW );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
}
}// namespace stilletchaser
