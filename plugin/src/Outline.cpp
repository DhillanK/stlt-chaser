#include "Outline.h"
#include <algorithm>
#include <cmath>
#include <string>
using namespace ffglex;

namespace stilletchaser
{
namespace
{
const char vertexShaderBody[] = R"(
layout( location = 0 ) in vec2 compPos;
out vec2 pixel;
out vec2 contentUV;
uniform vec2 compSize;
void main()
{
	gl_Position = compToClip( compPos );
	pixel       = compPos * compSize;
	contentUV   = vec2( compPos.x, 1.0 - compPos.y );
}
)";

// Distance from the slice's edge and position along its perimeter, then either a static
// band (with an inward feather) or `lineCount` tapered lines chasing around the edge.
const char fragmentShaderCode[] = R"(#version 410 core
uniform vec2 points[ 64 ];   // Slice outline in composition pixels, clockwise on screen.
uniform float along[ 64 ];   // Perimeter distance at each point.
uniform int pointCount;
uniform float perimeter;
uniform vec3 colour;
uniform float strength;      // This slice's outline opacity.
uniform float width;
uniform float feather;
uniform int animate;
uniform float head;          // Position of the first line's head, 0..1 around the perimeter.
uniform int direction;       // 1 clockwise, -1 anticlockwise, 0 both ways (mirrored).
uniform float lineLength;    // 0..1 of the perimeter.
uniform int lineCount;
uniform int useCurve;
uniform float curve[ 256 ];
uniform sampler2D litTexture;
uniform vec2 litMaxUV;
uniform int litOnly;         // Only where the lit texture shows something.
in vec2 pixel;
in vec2 contentUV;
out vec4 fragColor;

float taperAt( float behind )
{
	if( behind >= lineLength )
		return 0.0;
	float x = behind / lineLength;
	return useCurve != 0 ? curve[ int( x * 255.0 ) ] : 1.0 - x;
}

void main()
{
	float best = 1e20;
	float s    = 0.0;
	for( int i = 0; i < pointCount; ++i )
	{
		vec2 a  = points[ i ];
		vec2 b  = points[ ( i + 1 ) % pointCount ];
		vec2 ab = b - a;
		float t = clamp( dot( pixel - a, ab ) / max( dot( ab, ab ), 1e-6 ), 0.0, 1.0 );
		float d = distance( pixel, a + ab * t );
		if( d < best )
		{
			best = d;
			s    = ( along[ i ] + t * length( ab ) ) / perimeter;
		}
	}

	float alpha;
	if( animate == 0 )
	{
		// Full strength across the width, then a feather fading further in.
		alpha = feather > 0.0 ? clamp( 1.0 - ( best - width ) / feather, 0.0, 1.0 ) : clamp( width - best + 0.5, 0.0, 1.0 );
	}
	else
	{
		float line = 0.0;
		for( int i = 0; i < lineCount; ++i )
		{
			float h = fract( head + float( i ) / float( lineCount ) );
			if( direction >= 0 )
				line = max( line, taperAt( fract( h - s ) ) );
			if( direction <= 0 )
				line = max( line, taperAt( fract( s - ( 1.0 - h ) ) ) );
		}
		// The line thins towards its tail.
		alpha = clamp( width * line - best + 0.5, 0.0, 1.0 );
	}
	alpha *= strength;
	if( litOnly != 0 )
		alpha *= texture( litTexture, contentUV * litMaxUV ).a;
	if( alpha <= 0.0 )
		discard;
	fragColor = vec4( colour, 1.0 ) * alpha;
}
)";

float Hash( size_t index )
{
	uint32_t h = (uint32_t)index * 2654435761u + 0x9e3779b9u;
	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	return (float)( h & 0xffffff ) / (float)0x1000000;
}
}// namespace

bool OutlineRenderer::InitGL()
{
	const std::string vertex = std::string( CompositionVertexHeader() ) + vertexShaderBody;
	return shader.Compile( vertex, fragmentShaderCode ) && mesh.Init( { 2 } );
}

void OutlineRenderer::DeInitGL()
{
	shader.FreeGLResources();
	mesh.Release();
}

// A slice's own direction from the editor, reversed by Flip Direction. Auto slices run clockwise;
// for them Flip mirrors the stage instead: slices right of centre run clockwise, left of centre
// anticlockwise, centred ones both ways. Odd/Even reverses every other slice.
int OutlineRenderer::Direction( const OutlineStyle& style, const Slice& slice, Vec2 centre, int shownIndex ) const
{
	int direction = 1;
	if( slice.outline != OUTLINE_AUTO )
	{
		direction = slice.outline == OUTLINE_CW ? 1 : slice.outline == OUTLINE_CCW ? -1 : 0;
		if( style.flip )
			direction = -direction;
	}
	else if( style.flip )
	{
		if( std::fabs( centre.x - 0.5f ) < 0.02f )
			direction = 0;
		else
			direction = centre.x > 0.5f ? 1 : -1;
	}
	if( style.oddEven && shownIndex % 2 == 1 )
		direction = -direction;
	return direction;
}

// The outline in composition pixels, ordered clockwise on screen, with distances along it.
void OutlineRenderer::UploadOutline( const ChaseDocument& doc, const Slice& slice )
{
	std::vector< Vec2 > pts;
	for( const Vec2& p : slice.points )
		pts.push_back( { p.x * doc.compWidth, p.y * doc.compHeight } );
	float area = 0.f;
	for( size_t i = 0; i < pts.size(); ++i )
	{
		const Vec2& a = pts[ i ];
		const Vec2& b = pts[ ( i + 1 ) % pts.size() ];
		area += a.x * b.y - b.x * a.y;
	}
	// With y pointing down a positive area is clockwise on screen.
	if( area < 0.f )
		std::reverse( pts.begin(), pts.end() );
	std::vector< float > flat, along;
	float total = 0.f;
	for( size_t i = 0; i < pts.size(); ++i )
	{
		flat.insert( flat.end(), { pts[ i ].x, pts[ i ].y } );
		along.push_back( total );
		const Vec2& next = pts[ ( i + 1 ) % pts.size() ];
		total += std::hypot( next.x - pts[ i ].x, next.y - pts[ i ].y );
	}
	glUniform2fv( glGetUniformLocation( shader.GetGLID(), "points" ), (GLsizei)pts.size(), flat.data() );
	glUniform1fv( glGetUniformLocation( shader.GetGLID(), "along" ), (GLsizei)along.size(), along.data() );
	shader.Set( "pointCount", (int)pts.size() );
	shader.Set( "perimeter", std::max( total, 1e-3f ) );
}

void OutlineRenderer::Draw( const ChaseDocument& doc, const OutlineStyle& style, const std::vector< float >& strength, const FFGLTextureStruct* litTexture )
{
	vertices.clear();
	std::vector< std::pair< GLint, GLsizei > > ranges;
	for( const Slice& slice : doc.slices )
	{
		ranges.push_back( { (GLint)( vertices.size() / 2 ), (GLsizei)slice.indices.size() } );
		for( uint16_t i : slice.indices )
			vertices.insert( vertices.end(), { slice.points[ i ].x, slice.points[ i ].y } );
	}
	mesh.Upload( vertices );

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	ScopedSamplerActivation activateSampler( 0 );
	Scoped2DTextureBinding textureBinding( litTexture ? litTexture->Handle : 0 );
	if( litTexture )
	{
		const FFGLTexCoords maxCoords = GetMaxGLTexCoords( *litTexture );
		shader.Set( "litMaxUV", maxCoords.s, maxCoords.t );
	}
	shader.Set( "litTexture", 0 );
	shader.Set( "litOnly", litTexture ? 1 : 0 );
	shader.Set( "compSize", doc.compWidth, doc.compHeight );
	shader.Set( "colour", style.red, style.green, style.blue );
	shader.Set( "width", std::max( style.width, 0.f ) );
	shader.Set( "feather", std::max( style.feather, 0.f ) );
	shader.Set( "animate", style.animate ? 1 : 0 );
	shader.Set( "lineLength", std::clamp( style.length, 0.01f, 1.f ) );
	shader.Set( "lineCount", std::clamp( style.count, 1, MAX_LINES ) );
	shader.Set( "useCurve", style.envelope ? 1 : 0 );
	if( style.envelope )
		glUniform1fv( glGetUniformLocation( shader.GetGLID(), "curve" ), 256, style.envelope->lut.data() );

	// Outlines go over what is already drawn (premultiplied alpha).
	glEnable( GL_BLEND );
	glBlendEquation( GL_FUNC_ADD );
	glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	ScopedVAOBinding vaoBinding( mesh.VAO() );
	int shown = 0;
	for( size_t s = 0; s < doc.slices.size() && s < strength.size(); ++s )
	{
		const Slice& slice = doc.slices[ s ];
		if( strength[ s ] <= 0.f || slice.points.size() > MAX_POINTS )
			continue;
		UploadOutline( doc, slice );
		shader.Set( "strength", std::min( strength[ s ], 1.f ) );
		if( style.animate )
		{
			const Vec2 centre      = SliceCentre( slice );
			const float fromCentre = std::hypot( centre.x - 0.5f, centre.y - 0.5f );
			shader.Set( "head", style.phase + style.fan * fromCentre + style.randomise * Hash( s ) );
			shader.Set( "direction", Direction( style, slice, centre, shown ) );
		}
		glDrawArrays( GL_TRIANGLES, ranges[ s ].first, ranges[ s ].second );
		++shown;
	}
	glBlendFunc( GL_ONE, GL_ZERO );
	glDisable( GL_BLEND );
}
}// namespace stilletchaser
