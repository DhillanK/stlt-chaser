// STLT-SAMPLER: lights every slice of a view with the colour of the video under it, like a
// pixel-mapped LED stage: the average of the whole slice, or the colour at its centre.
// Once a frame the video is copied into a small mipmapped texture and each slice's colour is worked
// out into one texel of a colour texture: a grid of samples across the slice's own frame (so rotated
// slices sample along their own edges), only inside its outline. The slices are then drawn in it.
#include <FFGLSDK.h>
#include <ffglex/FFGLScopedFBOBinding.h>
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
	PID_SAMPLE,
	PID_COLOUR,
	PID_RED,
	PID_GREEN,
	PID_BLUE,
	PID_GAIN,
	PID_FLAT,
	PID_SHOW_ORIGINAL,
	PID_COUNT
};

enum Sample
{
	SAMPLE_AVERAGE,// the whole slice
	SAMPLE_CENTRE, // the middle of the slice
};

enum Colour
{
	COLOUR_VIDEO,// the video's own colour
	COLOUR_TINT, // its brightness, in the chosen colour
};

const int AVERAGE_GRID = 6;     // samples per side of a slice's frame
const int MAX_SAMPLE_SIZE = 1024;// the copy of the video is at most this wide or high

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
out vec2 comp;
void main()
{
	gl_Position = compToClip( compPos );
	comp        = compPos;
}
)";

// Pass 1, one texel per slice: the average of grid x grid samples across the slice's sampling frame
// (origin and edges, composition 0..1 with y down), at a mip level about the size of each cell.
// For a slice that is not a parallelogram the frame is its bounding box and only samples inside its
// outline count, so video beside the slice never colours it.
const char averageFragmentShader[] = R"(#version 410 core
uniform sampler2D sampled;
uniform vec2 origin;
uniform vec2 axisX;
uniform vec2 axisY;
uniform float lod;
uniform int grid;
uniform int pointCount;
uniform vec2 points[ 64 ];
out vec4 fragColor;
vec3 at( vec2 p )
{
	return textureLod( sampled, vec2( p.x, 1.0 - p.y ), lod ).rgb;
}
bool inside( vec2 p )
{
	bool in_ = false;
	for( int i = 0, j = pointCount - 1; i < pointCount; j = i++ )
	{
		vec2 a = points[ i ], b = points[ j ];
		if( ( a.y > p.y ) != ( b.y > p.y ) && p.x < ( b.x - a.x ) * ( p.y - a.y ) / ( b.y - a.y ) + a.x )
			in_ = !in_;
	}
	return in_;
}
void main()
{
	vec3 sum = vec3( 0.0 );
	int n    = 0;
	for( int j = 0; j < grid; ++j )
		for( int i = 0; i < grid; ++i )
		{
			vec2 f = ( vec2( i, j ) + 0.5 ) / float( grid );
			vec2 p = origin + axisX * f.x + axisY * f.y;
			if( pointCount > 0 && !inside( p ) )
				continue;
			sum += at( p );
			++n;
		}
	// A slice too thin for any sample to land inside it: its middle.
	fragColor = vec4( n > 0 ? sum / float( n ) : at( origin + ( axisX + axisY ) * 0.5 ), 1.0 );
}
)";

// Pass 2: each slice in its colour (from pass 1), tinted, scaled and blended with the video.
const char fragmentShaderBody[] = R"(
uniform sampler2D colours;
uniform ivec2 texel;
uniform sampler2D inputTexture;
uniform vec2 maxUV;
uniform int tinted;
uniform vec3 tint;
uniform float gain;
uniform float flatness;
in vec2 comp;
out vec4 fragColor;
void main()
{
	vec3 c = texelFetch( colours, texel, 0 ).rgb;
	if( tinted != 0 )
		c = tint * dot( c, vec3( 0.2126, 0.7152, 0.0722 ) );
	c *= gain;
	vec3 video = texture( inputTexture, vec2( comp.x, 1.0 - comp.y ) * maxUV ).rgb;
	fragColor  = vec4( mix( video, c, flatness ), 1.0 );
}
)";
}// namespace

class Sampler : public StilletchaserPlugin
{
public:
	Sampler()
	{
		SetMinInputs( 1 );
		SetMaxInputs( 1 );
		SetOptionParamInfo( PID_VIEW, "View", 1, 0.f );
		SetParamElementInfo( PID_VIEW, 0, "All", 0.f );
		SetOptionParamInfo( PID_SAMPLE, "Sample", 2, (float)SAMPLE_AVERAGE );
		SetParamElementInfo( PID_SAMPLE, SAMPLE_AVERAGE, "Whole Slice", (float)SAMPLE_AVERAGE );
		SetParamElementInfo( PID_SAMPLE, SAMPLE_CENTRE, "Slice Centre", (float)SAMPLE_CENTRE );
		SetOptionParamInfo( PID_COLOUR, "Colour", 2, (float)COLOUR_VIDEO );
		SetParamElementInfo( PID_COLOUR, COLOUR_VIDEO, "Video", (float)COLOUR_VIDEO );
		SetParamElementInfo( PID_COLOUR, COLOUR_TINT, "Brightness Tint", (float)COLOUR_TINT );
		SetParamInfo( PID_RED, "Red", FF_TYPE_RED, 1.f );
		SetParamInfo( PID_GREEN, "Green", FF_TYPE_GREEN, 1.f );
		SetParamInfo( PID_BLUE, "Blue", FF_TYPE_BLUE, 1.f );
		SetRangedParamInfo( PID_GAIN, "Gain", FF_TYPE_STANDARD, 1.f, 0.f, 4.f );
		SetRangedParamInfo( PID_FLAT, "Flat", FF_TYPE_STANDARD, 1.f, 0.f, 1.f );
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
		if( !shader.Compile( vertex, fragment ) || !averageShader.Compile( copyVertexShader, averageFragmentShader ) ||
			!copyShader.Compile( copyVertexShader, copyFragmentShader ) || !quad.Initialise() || !mesh.Init( { 2 } ) )
		{
			DeInitGL();
			return FF_FAIL;
		}
		glGenFramebuffers( 1, &fbo );
		// Looked up once: they are set for every slice of every frame.
		const GLuint average = averageShader.GetGLID();
		for( const char* name : { "origin", "axisX", "axisY", "lod", "grid", "pointCount", "points" } )
			averageUniforms.push_back( glGetUniformLocation( average, name ) );
		texelUniform = glGetUniformLocation( shader.GetGLID(), "texel" );
		return CFFGLPlugin::InitGL( vp );
	}

	FFResult DeInitGL() override
	{
		shader.FreeGLResources();
		averageShader.FreeGLResources();
		copyShader.FreeGLResources();
		quad.Release();
		mesh.Release();
		for( GLuint* texture : { &sampleTexture, &colourTexture } )
			if( *texture )
				glDeleteTextures( 1, texture );
		if( fbo )
			glDeleteFramebuffers( 1, &fbo );
		sampleTexture = colourTexture = fbo = 0;
		sampleWidth = sampleHeight = colourRows = 0;
		averageUniforms.clear();
		meshFor.reset();
		return FF_SUCCESS;
	}

	FFResult SetFloatParameter( unsigned int index, float value ) override
	{
		if( index >= PID_COUNT )
			return FF_FAIL;
		values[ index ] = value;
		if( index == PID_COLOUR )
			UpdateVisibility( true );
		return FF_SUCCESS;
	}

	float GetFloatParameter( unsigned int index ) override
	{
		return index < PID_COUNT ? values[ index ] : 0.f;
	}

	const char* GetShortName() override
	{
		static const char* sname = "STSampl";
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
		const FFGLTexCoords inputMax = GetMaxGLTexCoords( input );
		const View& view             = doc->views[ Option( values[ PID_VIEW ], (int)doc->views.size() ) ];

		// 1. The video shrunk into our own mipmapped texture, and each slice's colour worked out from it.
		if( !doc->slices.empty() )
		{
			if( meshFor != doc )
				Prepare();
			SampleSlices( input, inputMax, view, pGL->HostFBO );
		}

		// 2. The output: the incoming video, or nothing, under the slices.
		glClearColor( 0.f, 0.f, 0.f, 0.f );
		glClear( GL_COLOR_BUFFER_BIT );
		if( values[ PID_SHOW_ORIGINAL ] > 0.5f )
		{
			ScopedShaderBinding copyBinding( copyShader.GetGLID() );
			ScopedSamplerActivation activateSampler( 0 );
			Scoped2DTextureBinding textureBinding( input.Handle );
			copyShader.Set( "inputTexture", 0 );
			copyShader.Set( "maxUV", inputMax.s, inputMax.t );
			quad.Draw();
		}
		if( doc->slices.empty() || !colourTexture )
			return FF_SUCCESS;

		// 3. Every slice of the view, in its colour.
		ScopedShaderBinding shaderBinding( shader.GetGLID() );
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, colourTexture );
		glActiveTexture( GL_TEXTURE0 );
		Scoped2DTextureBinding textureBinding( input.Handle );
		shader.Set( "inputTexture", 0 );
		shader.Set( "colours", 1 );
		shader.Set( "maxUV", inputMax.s, inputMax.t );
		shader.Set( "tinted", Option( values[ PID_COLOUR ], 2 ) == COLOUR_TINT ? 1 : 0 );
		shader.Set( "tint", values[ PID_RED ], values[ PID_GREEN ], values[ PID_BLUE ] );
		shader.Set( "gain", std::max( values[ PID_GAIN ], 0.f ) );
		shader.Set( "flatness", std::clamp( values[ PID_FLAT ], 0.f, 1.f ) );
		ScopedVAOBinding vaoBinding( mesh.VAO() );
		for( size_t s = 0; s < doc->slices.size(); ++s )
		{
			if( !view.includes[ s ] )
				continue;
			glUniform2i( texelUniform, (GLint)( s % COLOURS_WIDE ), (GLint)( s / COLOURS_WIDE ) );
			glDrawArrays( GL_TRIANGLES, ranges[ s ].first, ranges[ s ].second );
		}
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glActiveTexture( GL_TEXTURE0 );
		return FF_SUCCESS;
	}

private:
	static const int COLOURS_WIDE = 256;// slice colours per row of the colour texture

	// Where a slice is sampled: its own frame when it is a parallelogram (rotated rectangles included),
	// otherwise its bounding box with its outline, so only samples inside the outline count.
	struct SampleFrame
	{
		Vec2 origin, axisX, axisY;
		std::vector< float > outline;// x, y pairs; empty when the frame is the slice
	};

	// The mip level whose texels are about one cell of the sampling grid, one finer so the samples
	// near a slice's edge reach less into its neighbours.
	float SampleLevel( const SampleFrame& frame, int grid ) const
	{
		const float cellX = std::hypot( frame.axisX.x * sampleWidth, frame.axisX.y * sampleHeight ) / (float)grid;
		const float cellY = std::hypot( frame.axisY.x * sampleWidth, frame.axisY.y * sampleHeight ) / (float)grid;
		return std::max( 0.f, std::log2( std::sqrt( std::max( cellX * cellY, 1.f ) ) ) - 1.f );
	}

	void SampleSlices( const FFGLTextureStruct& input, const FFGLTexCoords& inputMax, const View& view, GLuint hostFBO )
	{
		const float scale = std::min( 1.f, (float)MAX_SAMPLE_SIZE / (float)std::max( { input.Width, input.Height, 1u } ) );
		const int width   = std::max( 1, (int)std::lround( input.Width * scale ) );
		const int height  = std::max( 1, (int)std::lround( input.Height * scale ) );
		if( width != sampleWidth || height != sampleHeight || !sampleTexture )
		{
			if( sampleTexture )
				glDeleteTextures( 1, &sampleTexture );
			// Level 0 here; glGenerateMipmap makes the smaller levels (glTexStorage needs GL 4.2, macOS has 4.1).
			sampleTexture = NewTexture( width, height, GL_LINEAR_MIPMAP_LINEAR );
			sampleWidth   = width;
			sampleHeight  = height;
		}
		const int rows = std::max( 1, (int)( ( doc->slices.size() + COLOURS_WIDE - 1 ) / COLOURS_WIDE ) );
		if( rows != colourRows || !colourTexture )
		{
			if( colourTexture )
				glDeleteTextures( 1, &colourTexture );
			colourTexture = NewTexture( COLOURS_WIDE, rows, GL_NEAREST );
			colourRows    = rows;
		}

		GLint viewport[ 4 ];
		glGetIntegerv( GL_VIEWPORT, viewport );
		{
			ScopedFBOBinding fboBinding( fbo, ScopedFBOBinding::RB_REVERT );
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sampleTexture, 0 );
			glViewport( 0, 0, sampleWidth, sampleHeight );
			{
				ScopedShaderBinding copyBinding( copyShader.GetGLID() );
				ScopedSamplerActivation activateSampler( 0 );
				Scoped2DTextureBinding textureBinding( input.Handle );
				copyShader.Set( "inputTexture", 0 );
				copyShader.Set( "maxUV", inputMax.s, inputMax.t );
				quad.Draw();
			}
			{
				Scoped2DTextureBinding binding( sampleTexture );
				glGenerateMipmap( GL_TEXTURE_2D );
			}

			// One texel per slice of the view.
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colourTexture, 0 );
			const bool centre = Option( values[ PID_SAMPLE ], 2 ) == SAMPLE_CENTRE;
			const int grid    = centre ? 1 : AVERAGE_GRID;
			ScopedShaderBinding averageBinding( averageShader.GetGLID() );
			ScopedSamplerActivation activateSampler( 0 );
			Scoped2DTextureBinding textureBinding( sampleTexture );
			averageShader.Set( "sampled", 0 );
			glUniform1i( averageUniforms[ 4 ], grid );
			for( size_t s = 0; s < doc->slices.size(); ++s )
			{
				if( !view.includes[ s ] )
					continue;
				const SampleFrame& frame = frames[ s ];
				glViewport( (GLint)( s % COLOURS_WIDE ), (GLint)( s / COLOURS_WIDE ), 1, 1 );
				glUniform2f( averageUniforms[ 0 ], frame.origin.x, frame.origin.y );
				glUniform2f( averageUniforms[ 1 ], frame.axisX.x, frame.axisX.y );
				glUniform2f( averageUniforms[ 2 ], frame.axisY.x, frame.axisY.y );
				glUniform1f( averageUniforms[ 3 ], centre ? 0.f : SampleLevel( frame, grid ) );
				// The slice's middle, for Slice Centre, even for shapes whose middle is outside them.
				const int count = centre ? 0 : (int)( frame.outline.size() / 2 );
				glUniform1i( averageUniforms[ 5 ], count );
				if( count )
					glUniform2fv( averageUniforms[ 6 ], count, frame.outline.data() );
				quad.Draw();
			}
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0 );
		}
		// RB_REVERT rebinds whatever was bound before; make sure that is the host's target.
		glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
		glViewport( viewport[ 0 ], viewport[ 1 ], viewport[ 2 ], viewport[ 3 ] );
	}

	static GLuint NewTexture( int width, int height, GLint minFilter )
	{
		GLuint texture = 0;
		glGenTextures( 1, &texture );
		Scoped2DTextureBinding binding( texture );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, minFilter == GL_NEAREST ? GL_NEAREST : GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		return texture;
	}

	// The slices' triangles and sampling frames, made again only when the document changes.
	void Prepare()
	{
		std::vector< float > vertices;
		ranges.clear();
		frames.clear();
		for( const Slice& slice : doc->slices )
		{
			ranges.push_back( { (GLint)( vertices.size() / 2 ), (GLsizei)slice.indices.size() } );
			for( uint16_t i : slice.indices )
				vertices.insert( vertices.end(), { slice.points[ i ].x, slice.points[ i ].y } );
			frames.push_back( FrameOf( slice ) );
		}
		mesh.Upload( vertices );
		meshFor = doc;
	}

	static SampleFrame FrameOf( const Slice& slice )
	{
		const auto& p = slice.points;
		if( p.size() == 4 )
		{
			// A parallelogram's opposite corners share a middle.
			const float dx = p[ 0 ].x + p[ 2 ].x - p[ 1 ].x - p[ 3 ].x;
			const float dy = p[ 0 ].y + p[ 2 ].y - p[ 1 ].y - p[ 3 ].y;
			const float size = std::max( std::hypot( slice.axisX.x, slice.axisX.y ), std::hypot( slice.axisY.x, slice.axisY.y ) );
			if( std::hypot( dx, dy ) <= 0.01f * size )
				return { slice.origin, slice.axisX, slice.axisY, {} };
		}
		float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
		for( const Vec2& v : p )
		{
			minX = std::min( minX, v.x );
			minY = std::min( minY, v.y );
			maxX = std::max( maxX, v.x );
			maxY = std::max( maxY, v.y );
		}
		SampleFrame frame{ { minX, minY }, { maxX - minX, 0.f }, { 0.f, maxY - minY }, {} };
		// Up to 64 corners can be tested; a bigger outline samples its whole bounding box.
		if( p.size() <= 64 )
			for( const Vec2& v : p )
				frame.outline.insert( frame.outline.end(), { v.x, v.y } );
		return frame;
	}

	void RefreshMenus( bool raiseEvents )
	{
		const auto views = ViewNames( *doc );
		SetParamElements( PID_VIEW, views, Indices( views.size() ), raiseEvents );
	}

	// The tint only matters for Brightness Tint.
	void UpdateVisibility( bool raiseEvents )
	{
		const bool tint = Option( values[ PID_COLOUR ], 2 ) == COLOUR_TINT;
		for( int id : { PID_RED, PID_GREEN, PID_BLUE } )
			SetParamVisibility( id, tint, raiseEvents );
	}

	float GetDefault( int index )
	{
		switch( index )
		{
		case PID_RED:
		case PID_GREEN:
		case PID_BLUE:
		case PID_GAIN:
		case PID_FLAT: return 1.f;
		default:       return 0.f;
		}
	}

	FFGLShader shader, averageShader, copyShader;
	FFGLScreenQuad quad;
	FloatVertexBuffer mesh;
	std::vector< std::pair< GLint, GLsizei > > ranges;
	std::vector< SampleFrame > frames;
	std::shared_ptr< const ChaseDocument > meshFor;// the document the mesh and frames were made from
	std::vector< GLint > averageUniforms;
	GLint texelUniform = -1;
	GLuint fbo = 0, sampleTexture = 0, colourTexture = 0;
	int sampleWidth = 0, sampleHeight = 0, colourRows = 0;
	std::shared_ptr< const ChaseDocument > doc;
	uint64_t docVersion = 0;
	float values[ PID_COUNT ];
};

static CFFGLPluginInfo PluginInfo(
	PluginFactory< Sampler >,
	"STSM",         // Plugin unique ID
	"STLT-SAMPLER", // Plugin name
	2,              // API major version number
	1,              // API minor version number
	1,              // Plugin major version number
	4,              // Plugin minor version number
	FF_EFFECT,
	"Lights Advanced Output slices with the colour of the video under them",
	"Stilletchaser" );
