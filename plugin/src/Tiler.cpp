// STLT-TILER: animated pixel patterns inside every slice of a view. Each slice is cut into square
// tiles along its own edges (Tiles counts them along its long edge) and a pattern runs over them,
// driven by Phase (animate it in Resolume). Patterns run in each slice's direction from the
// editor's Directions page (Auto: as the Wiper would), so a stage set up once moves the same way.
#include <FFGLSDK.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include "ChaseDocument.h"
#include "PluginCommon.h"
using namespace ffglex;
using namespace stilletchaser;

namespace
{
enum ParamID
{
	PID_VIEW,
	PID_PATTERN,
	PID_PHASE,
	PID_TILES,
	PID_DENSITY,
	PID_GAP,
	PID_SCALING,
	PID_SOURCE,
	PID_RED,
	PID_GREEN,
	PID_BLUE,
	PID_SHOW_ORIGINAL,
	PID_COUNT
};

enum Pattern
{
	PATTERN_CHECKER,// tiles alternate, swapping halfway through Phase
	PATTERN_STRIPES,// bands of tiles moving along the slice
	PATTERN_SCAN,   // one band sweeping from start to end
	PATTERN_SPARKLE,// random tiles, a new set on each of 16 steps of Phase
	PATTERN_WAVE,   // brightness rolling along the slice
	PATTERNS
};

enum Source
{
	SOURCE_VIDEO,
	SOURCE_COLOUR,
};

const int MAX_TILES = 64;

const char copyVertexShader[] = R"(#version 410 core
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
)";

const char copyFragmentShader[] = R"(#version 410 core
uniform sampler2D inputTexture;
uniform vec2 maxUV;
in vec2 uv;
out vec4 fragColor;
void main()
{
	fragColor = texture( inputTexture, uv * maxUV );
}
)";

const char vertexShaderBody[] = R"(
layout( location = 0 ) in vec2 compPos;
layout( location = 1 ) in vec2 contentUV;
layout( location = 2 ) in vec2 sliceLocal;
out vec2 uv;
out vec2 local;
void main()
{
	gl_Position = compToClip( compPos );
	uv          = contentUV;
	local       = sliceLocal;
}
)";

// `local` runs 0..1 along the slice's own x and y edges. The pattern is worked out per tile, so a
// tile is always one level; `along` is the tile's place in the slice's direction (0 = where it starts).
const char fragmentShaderBody[] = R"(
uniform sampler2D inputTexture;
uniform vec2 maxUV;
uniform int useColour;
uniform vec3 colour;
uniform int pattern;
uniform float phase;
uniform float density;
uniform float gap;
uniform vec2 tiles;
uniform int alongY;
uniform int reversed;
uniform int mirror;
uniform float seed;
in vec2 uv;
in vec2 local;
out vec4 fragColor;
float hash( vec2 p )
{
	return fract( sin( dot( p, vec2( 127.1, 311.7 ) ) + seed * 17.13 ) * 43758.5453 );
}
void main()
{
	vec2 g    = clamp( local, 0.0, 0.99999 ) * tiles;
	vec2 cell = floor( g );
	vec2 f    = g - cell;
	if( any( lessThan( f, vec2( gap * 0.5 ) ) ) || any( greaterThan( f, vec2( 1.0 - gap * 0.5 ) ) ) )
		discard;
	float count = alongY != 0 ? tiles.y : tiles.x;
	float n     = alongY != 0 ? cell.y : cell.x;
	float along = ( n + 0.5 ) / count;
	if( mirror != 0 )
		along = abs( along - 0.5 ) * 2.0;
	if( reversed != 0 )
		along = 1.0 - along;
	float p = fract( phase );
	float a = 0.0;
	if( pattern == 0 )
		a = mod( cell.x + cell.y + floor( p * 2.0 ), 2.0 );
	else if( pattern == 1 )
		a = step( fract( along * count * 0.25 - p ), density );
	else if( pattern == 2 )
	{
		float d = abs( along - p );
		a       = step( d, max( density * 0.5, 0.5 / count ) );
	}
	else if( pattern == 3 )
		a = step( hash( cell + floor( p * 16.0 ) * vec2( 7.0, 3.0 ) ), density );
	else
		a = 0.5 + 0.5 * cos( 6.2831853 * ( along * 2.0 - p ) );
	if( a <= 0.0 )
		discard;
	vec4 source = useColour != 0 ? vec4( colour, 1.0 ) : texture( inputTexture, uv * maxUV );
	fragColor   = source * a;
}
)";
}// namespace

class Tiler : public StilletchaserPlugin
{
public:
	Tiler()
	{
		SetMinInputs( 1 );
		SetMaxInputs( 1 );
		SetOptionParamInfo( PID_VIEW, "View", 1, 0.f );
		SetParamElementInfo( PID_VIEW, 0, "All", 0.f );
		SetOptionParamInfo( PID_PATTERN, "Pattern", PATTERNS, (float)PATTERN_CHECKER );
		SetParamElementInfo( PID_PATTERN, PATTERN_CHECKER, "Checker", (float)PATTERN_CHECKER );
		SetParamElementInfo( PID_PATTERN, PATTERN_STRIPES, "Stripes", (float)PATTERN_STRIPES );
		SetParamElementInfo( PID_PATTERN, PATTERN_SCAN, "Scan", (float)PATTERN_SCAN );
		SetParamElementInfo( PID_PATTERN, PATTERN_SPARKLE, "Sparkle", (float)PATTERN_SPARKLE );
		SetParamElementInfo( PID_PATTERN, PATTERN_WAVE, "Wave", (float)PATTERN_WAVE );
		SetParamInfo( PID_PHASE, "Phase", FF_TYPE_STANDARD, 0.f );
		SetRangedParamInfo( PID_TILES, "Tiles", FF_TYPE_INTEGER, 8.f, 1.f, (float)MAX_TILES );
		SetRangedParamInfo( PID_DENSITY, "Density", FF_TYPE_STANDARD, 0.5f, 0.f, 1.f );
		SetRangedParamInfo( PID_GAP, "Gap", FF_TYPE_STANDARD, 0.1f, 0.f, 0.9f );
		SetOptionParamInfo( PID_SCALING, "Scaling", 3, (float)SCALING_MASK );
		SetParamElementInfo( PID_SCALING, SCALING_MASK, "Mask", (float)SCALING_MASK );
		SetParamElementInfo( PID_SCALING, SCALING_FILL, "Fill", (float)SCALING_FILL );
		SetParamElementInfo( PID_SCALING, SCALING_STRETCH, "Stretch", (float)SCALING_STRETCH );
		SetOptionParamInfo( PID_SOURCE, "Source", 2, (float)SOURCE_VIDEO );
		SetParamElementInfo( PID_SOURCE, SOURCE_VIDEO, "Video", (float)SOURCE_VIDEO );
		SetParamElementInfo( PID_SOURCE, SOURCE_COLOUR, "Solid Color", (float)SOURCE_COLOUR );
		SetParamInfo( PID_RED, "Red", FF_TYPE_RED, 1.f );
		SetParamInfo( PID_GREEN, "Green", FF_TYPE_GREEN, 1.f );
		SetParamInfo( PID_BLUE, "Blue", FF_TYPE_BLUE, 1.f );
		SetParamInfo( PID_SHOW_ORIGINAL, "Show Original", FF_TYPE_BOOLEAN, false );
		for( int i = 0; i < PID_COUNT; ++i )
			values[ i ] = GetDefault( i );
		UpdateVisibility( false );

		doc        = DocumentWatcher::Instance().Current();
		docVersion = DocumentWatcher::Instance().Version();
		RefreshMenus( false );
	}

	FFResult InitGL( const FFGLViewportStruct* vp ) override
	{
		const std::string vertex   = std::string( CompositionVertexHeader() ) + vertexShaderBody;
		const std::string fragment = std::string( "#version 410 core\n" ) + fragmentShaderBody;
		if( !shader.Compile( vertex, fragment ) || !copyShader.Compile( copyVertexShader, copyFragmentShader ) || !quad.Initialise() ||
			!mesh.Init( { 2, 2, 2 } ) )
		{
			DeInitGL();
			return FF_FAIL;
		}
		const GLuint id = shader.GetGLID();
		for( const char* name : { "tiles", "alongY", "reversed", "mirror", "seed" } )
			uniforms.push_back( glGetUniformLocation( id, name ) );
		return CFFGLPlugin::InitGL( vp );
	}

	FFResult DeInitGL() override
	{
		shader.FreeGLResources();
		copyShader.FreeGLResources();
		quad.Release();
		mesh.Release();
		uniforms.clear();
		return FF_SUCCESS;
	}

	FFResult SetFloatParameter( unsigned int index, float value ) override
	{
		if( index >= PID_COUNT )
			return FF_FAIL;
		values[ index ] = value;
		if( index == PID_SOURCE || index == PID_PATTERN )
			UpdateVisibility( true );
		return FF_SUCCESS;
	}

	float GetFloatParameter( unsigned int index ) override
	{
		return index < PID_COUNT ? values[ index ] : 0.f;
	}

	const char* GetShortName() override
	{
		static const char* sname = "STTiler";
		return sname;
	}

	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override
	{
		if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
			return FF_FAIL;
		const FFGLTextureStruct& input = *pGL->inputTextures[ 0 ];
		DocumentWatcher& watcher       = DocumentWatcher::Instance();
		doc                            = watcher.Current();
		if( watcher.Version() != docVersion )
		{
			docVersion = watcher.Version();
			RefreshMenus( true );
		}

		glClearColor( 0.f, 0.f, 0.f, 0.f );
		glClear( GL_COLOR_BUFFER_BIT );
		const FFGLTexCoords inputMax = GetMaxGLTexCoords( input );
		if( values[ PID_SHOW_ORIGINAL ] > 0.5f )
		{
			ScopedShaderBinding copyBinding( copyShader.GetGLID() );
			ScopedSamplerActivation activateSampler( 0 );
			Scoped2DTextureBinding textureBinding( input.Handle );
			copyShader.Set( "inputTexture", 0 );
			copyShader.Set( "maxUV", inputMax.s, inputMax.t );
			quad.Draw();
		}
		if( doc->slices.empty() )
			return FF_SUCCESS;

		BuildMesh( input );
		const View& view  = doc->views[ Option( values[ PID_VIEW ], (int)doc->views.size() ) ];
		const bool colour = Option( values[ PID_SOURCE ], 2 ) == SOURCE_COLOUR;
		const float tiles = std::clamp( std::round( values[ PID_TILES ] ), 1.f, (float)MAX_TILES );

		ScopedShaderBinding shaderBinding( shader.GetGLID() );
		ScopedSamplerActivation activateSampler( 0 );
		Scoped2DTextureBinding textureBinding( input.Handle );
		shader.Set( "inputTexture", 0 );
		shader.Set( "maxUV", inputMax.s, inputMax.t );
		shader.Set( "useColour", colour ? 1 : 0 );
		shader.Set( "colour", values[ PID_RED ], values[ PID_GREEN ], values[ PID_BLUE ] );
		shader.Set( "pattern", Option( values[ PID_PATTERN ], PATTERNS ) );
		shader.Set( "phase", values[ PID_PHASE ] );
		shader.Set( "density", std::clamp( values[ PID_DENSITY ], 0.f, 1.f ) );
		shader.Set( "gap", std::clamp( values[ PID_GAP ], 0.f, 0.9f ) );

		// Tiles go over what is already there (premultiplied).
		glEnable( GL_BLEND );
		glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
		ScopedVAOBinding vaoBinding( mesh.VAO() );
		for( size_t s = 0; s < doc->slices.size(); ++s )
		{
			if( !view.includes[ s ] )
				continue;
			const Slice& slice = doc->slices[ s ];
			// Square tiles: Tiles along the long edge, as many as fit along the short one.
			const Vec2 size   = SliceSize( *doc, slice );
			const float ratio = size.x > 0.f && size.y > 0.f ? std::min( size.x, size.y ) / std::max( size.x, size.y ) : 1.f;
			const float other = std::max( 1.f, std::round( tiles * ratio ) );
			const WipeDirection direction = SliceWipeDirection( *doc, slice, false );
			glUniform2f( uniforms[ 0 ], size.x >= size.y ? tiles : other, size.x >= size.y ? other : tiles );
			glUniform1i( uniforms[ 1 ], direction.alongY ? 1 : 0 );
			glUniform1i( uniforms[ 2 ], direction.reversed ? 1 : 0 );
			glUniform1i( uniforms[ 3 ], direction.mirror ? 1 : 0 );
			glUniform1f( uniforms[ 4 ], (float)( s % 997 ) );
			glDrawArrays( GL_TRIANGLES, ranges[ s ].first, ranges[ s ].second );
		}
		glBlendFunc( GL_ONE, GL_ZERO );
		glDisable( GL_BLEND );
		return FF_SUCCESS;
	}

private:
	void BuildMesh( const FFGLTextureStruct& input )
	{
		std::vector< float > vertices;
		ranges.clear();
		const int scaling         = Option( values[ PID_SCALING ], 3 );
		const float contentAspect = input.Height ? (float)input.Width / (float)input.Height : 1.f;
		for( const Slice& slice : doc->slices )
		{
			ranges.push_back( { (GLint)( vertices.size() / 6 ), (GLsizei)slice.indices.size() } );
			for( uint16_t i : slice.indices )
			{
				const Vec2 p     = slice.points[ i ];
				const Vec2 uv    = ContentUV( *doc, slice, p, scaling, contentAspect );
				const Vec2 local = SliceLocal( slice, p );
				vertices.insert( vertices.end(), { p.x, p.y, uv.x, uv.y, local.x, local.y } );
			}
		}
		mesh.Upload( vertices );
	}

	void RefreshMenus( bool raiseEvents )
	{
		const auto views = ViewNames( *doc );
		SetParamElements( PID_VIEW, views, Indices( views.size() ), raiseEvents );
	}

	// The colour only for Solid Color; Density only for the patterns that use it.
	void UpdateVisibility( bool raiseEvents )
	{
		const bool colour  = Option( values[ PID_SOURCE ], 2 ) == SOURCE_COLOUR;
		const int pattern  = Option( values[ PID_PATTERN ], PATTERNS );
		for( int id : { PID_RED, PID_GREEN, PID_BLUE } )
			SetParamVisibility( id, colour, raiseEvents );
		SetParamVisibility( PID_DENSITY, pattern == PATTERN_STRIPES || pattern == PATTERN_SCAN || pattern == PATTERN_SPARKLE, raiseEvents );
	}

	float GetDefault( int index )
	{
		switch( index )
		{
		case PID_TILES:   return 8.f;
		case PID_DENSITY: return 0.5f;
		case PID_GAP:     return 0.1f;
		case PID_RED:
		case PID_GREEN:
		case PID_BLUE:    return 1.f;
		default:          return 0.f;
		}
	}

	FFGLShader shader, copyShader;
	FFGLScreenQuad quad;
	FloatVertexBuffer mesh;
	std::vector< std::pair< GLint, GLsizei > > ranges;
	std::vector< GLint > uniforms;
	std::shared_ptr< const ChaseDocument > doc;
	uint64_t docVersion = 0;
	float values[ PID_COUNT ];
};

static CFFGLPluginInfo PluginInfo(
	PluginFactory< Tiler >,
	"STTL",       // Plugin unique ID
	"STLT-TILER", // Plugin name
	2,            // API major version number
	1,            // API minor version number
	1,            // Plugin major version number
	4,            // Plugin minor version number
	FF_EFFECT,
	"Animated tile patterns inside Advanced Output slices",
	"Stilletchaser" );
