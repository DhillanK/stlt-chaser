// STLT-SAMPLER: lights every slice of a view with the colour of the video under it, like a
// pixel-mapped LED stage: the average of the whole slice, or the colour at its centre.
// The video is copied once a frame into a small mipmapped texture; each slice then averages a
// grid of samples taken across its own frame (so rotated slices sample along their own edges).
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

// One slice at a time: its frame (origin and edges, composition 0..1 with y down) is sampled
// grid x grid times at a mip level about the size of each cell, so the average covers the slice.
const char fragmentShaderBody[] = R"(
uniform sampler2D sampled;
uniform sampler2D inputTexture;
uniform vec2 maxUV;
uniform vec2 origin;
uniform vec2 axisX;
uniform vec2 axisY;
uniform float lod;
uniform int grid;
uniform int tinted;
uniform vec3 tint;
uniform float gain;
uniform float flatness;
in vec2 comp;
out vec4 fragColor;
vec3 at( vec2 p )
{
	return textureLod( sampled, vec2( p.x, 1.0 - p.y ), lod ).rgb;
}
void main()
{
	vec3 sum = vec3( 0.0 );
	for( int j = 0; j < grid; ++j )
		for( int i = 0; i < grid; ++i )
		{
			vec2 f = ( vec2( i, j ) + 0.5 ) / float( grid );
			sum += at( origin + axisX * f.x + axisY * f.y );
		}
	vec3 c = sum / float( grid * grid );
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
		if( !shader.Compile( vertex, fragment ) || !copyShader.Compile( copyVertexShader, copyFragmentShader ) || !quad.Initialise() ||
			!mesh.Init( { 2 } ) )
		{
			DeInitGL();
			return FF_FAIL;
		}
		glGenFramebuffers( 1, &fbo );
		// Looked up once: they are set for every slice of every frame.
		const GLuint id = shader.GetGLID();
		for( const char* name : { "origin", "axisX", "axisY", "lod", "grid" } )
			uniforms.push_back( glGetUniformLocation( id, name ) );
		return CFFGLPlugin::InitGL( vp );
	}

	FFResult DeInitGL() override
	{
		shader.FreeGLResources();
		copyShader.FreeGLResources();
		quad.Release();
		mesh.Release();
		if( sampleTexture )
			glDeleteTextures( 1, &sampleTexture );
		if( fbo )
			glDeleteFramebuffers( 1, &fbo );
		sampleTexture = fbo = 0;
		sampleWidth = sampleHeight = 0;
		uniforms.clear();
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
			meshFor.reset();
		}
		const FFGLTexCoords inputMax = GetMaxGLTexCoords( input );

		// 1. The video, shrunk into our own texture with mipmaps.
		if( !doc->slices.empty() )
			CopyInput( input, inputMax, pGL->HostFBO );

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
		if( doc->slices.empty() || !sampleTexture )
			return FF_SUCCESS;

		// 3. Every slice of the view, in its sampled colour.
		if( meshFor != doc )
			BuildMesh();
		const View& view   = doc->views[ Option( values[ PID_VIEW ], (int)doc->views.size() ) ];
		const bool centre  = Option( values[ PID_SAMPLE ], 2 ) == SAMPLE_CENTRE;
		const int grid     = centre ? 1 : AVERAGE_GRID;

		ScopedShaderBinding shaderBinding( shader.GetGLID() );
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, sampleTexture );
		glActiveTexture( GL_TEXTURE0 );
		Scoped2DTextureBinding textureBinding( input.Handle );
		shader.Set( "inputTexture", 0 );
		shader.Set( "sampled", 1 );
		shader.Set( "maxUV", inputMax.s, inputMax.t );
		shader.Set( "tinted", Option( values[ PID_COLOUR ], 2 ) == COLOUR_TINT ? 1 : 0 );
		shader.Set( "tint", values[ PID_RED ], values[ PID_GREEN ], values[ PID_BLUE ] );
		shader.Set( "gain", std::max( values[ PID_GAIN ], 0.f ) );
		shader.Set( "flatness", std::clamp( values[ PID_FLAT ], 0.f, 1.f ) );
		glUniform1i( uniforms[ 4 ], grid );

		ScopedVAOBinding vaoBinding( mesh.VAO() );
		for( size_t s = 0; s < doc->slices.size(); ++s )
		{
			if( !view.includes[ s ] )
				continue;
			const Slice& slice = doc->slices[ s ];
			glUniform2f( uniforms[ 0 ], slice.origin.x, slice.origin.y );
			glUniform2f( uniforms[ 1 ], slice.axisX.x, slice.axisX.y );
			glUniform2f( uniforms[ 2 ], slice.axisY.x, slice.axisY.y );
			glUniform1f( uniforms[ 3 ], centre ? 0.f : SampleLevel( slice, grid ) );
			glDrawArrays( GL_TRIANGLES, ranges[ s ].first, ranges[ s ].second );
		}
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glActiveTexture( GL_TEXTURE0 );
		return FF_SUCCESS;
	}

private:
	// The mip level whose texels are about one cell of the slice's sampling grid.
	float SampleLevel( const Slice& slice, int grid ) const
	{
		const float cellX = std::hypot( slice.axisX.x * sampleWidth, slice.axisX.y * sampleHeight ) / (float)grid;
		const float cellY = std::hypot( slice.axisY.x * sampleWidth, slice.axisY.y * sampleHeight ) / (float)grid;
		const float cell  = std::sqrt( std::max( cellX * cellY, 1.f ) );
		// One level finer than a cell: the samples near a slice's edge reach less into its neighbours.
		return std::max( 0.f, std::log2( cell ) - 1.f );
	}

	void CopyInput( const FFGLTextureStruct& input, const FFGLTexCoords& inputMax, GLuint hostFBO )
	{
		const float scale = std::min( 1.f, (float)MAX_SAMPLE_SIZE / (float)std::max( { input.Width, input.Height, 1u } ) );
		const int width   = std::max( 1, (int)std::lround( input.Width * scale ) );
		const int height  = std::max( 1, (int)std::lround( input.Height * scale ) );
		if( width != sampleWidth || height != sampleHeight || !sampleTexture )
		{
			if( sampleTexture )
				glDeleteTextures( 1, &sampleTexture );
			glGenTextures( 1, &sampleTexture );
			Scoped2DTextureBinding binding( sampleTexture );
			// Level 0 here; glGenerateMipmap makes the smaller levels (glTexStorage needs GL 4.2, macOS has 4.1).
			glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
			sampleWidth  = width;
			sampleHeight = height;
		}
		GLint viewport[ 4 ];
		glGetIntegerv( GL_VIEWPORT, viewport );
		{
			ScopedFBOBinding fboBinding( fbo, ScopedFBOBinding::RB_REVERT );
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sampleTexture, 0 );
			glViewport( 0, 0, sampleWidth, sampleHeight );
			ScopedShaderBinding copyBinding( copyShader.GetGLID() );
			ScopedSamplerActivation activateSampler( 0 );
			Scoped2DTextureBinding textureBinding( input.Handle );
			copyShader.Set( "inputTexture", 0 );
			copyShader.Set( "maxUV", inputMax.s, inputMax.t );
			quad.Draw();
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0 );
		}
		{
			Scoped2DTextureBinding binding( sampleTexture );
			glGenerateMipmap( GL_TEXTURE_2D );
		}
		// RB_REVERT rebinds whatever was bound before; make sure that is the host's target.
		glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
		glViewport( viewport[ 0 ], viewport[ 1 ], viewport[ 2 ], viewport[ 3 ] );
	}

	// The slices' triangles, made again only when the document changes.
	void BuildMesh()
	{
		std::vector< float > vertices;
		ranges.clear();
		for( const Slice& slice : doc->slices )
		{
			ranges.push_back( { (GLint)( vertices.size() / 2 ), (GLsizei)slice.indices.size() } );
			for( uint16_t i : slice.indices )
				vertices.insert( vertices.end(), { slice.points[ i ].x, slice.points[ i ].y } );
		}
		mesh.Upload( vertices );
		meshFor = doc;
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

	FFGLShader shader, copyShader;
	FFGLScreenQuad quad;
	FloatVertexBuffer mesh;
	std::vector< std::pair< GLint, GLsizei > > ranges;
	std::shared_ptr< const ChaseDocument > meshFor;// the document the mesh was made from
	std::vector< GLint > uniforms;
	GLuint fbo = 0, sampleTexture = 0;
	int sampleWidth = 0, sampleHeight = 0;
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
