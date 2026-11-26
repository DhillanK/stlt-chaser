// STLT-WIPER: each step of a sequence sends a gradient wipe across its slices.
// Step (0..1) runs through the sequence Loops times; Resolume animates it (BPM sync, timeline, FFT).
// Direction is automatic: vertical slices wipe bottom to top, horizontal ones wipe away from
// the centre of the stage (left to right on the right half, right to left on the left half).
#include <FFGLSDK.h>
#include <memory>
#include "ChaseDocument.h"
#include "PluginCommon.h"
using namespace ffglex;
using namespace stilletchaser;

namespace
{
enum ParamID
{
	PID_SEQUENCE,
	PID_STEP,
	PID_SCALING,
	PID_LENGTH,
	PID_OVERLAP,
	PID_LOOPS,
	PID_FLIP,
	PID_ODD_EVEN,
	PID_SWAP,
	PID_ENVELOPE,
	PID_SOURCE,
	PID_RED,
	PID_GREEN,
	PID_BLUE,
	PID_SHOW_ORIGINAL,
	PID_COUNT
};

enum Source
{
	SOURCE_VIDEO,
	SOURCE_COLOUR,
};

const int MAX_LOOPS = MAX_WIPE_LOOPS;

// Wipes show the incoming video, or a solid colour.
const char fragmentShaderBody[] = R"(
uniform sampler2D inputTexture;
uniform vec2 maxUV;
uniform int useColour;
uniform vec3 colour;
in vec2 uv;
in vec2 local;
out vec4 fragColor;
void main()
{
	float a = wipeAlpha( local );
	if( a <= 0.0 )
		discard;
	vec4 source = useColour != 0 ? vec4( colour, 1.0 ) : texture( inputTexture, uv * maxUV );
	fragColor   = source * a;
}
)";

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
}// namespace

class Wiper : public StilletchaserPlugin
{
public:
	Wiper()
	{
		SetMinInputs( 1 );
		SetMaxInputs( 1 );
		SetOptionParamInfo( PID_SEQUENCE, "Sequence", 1, 0.f );
		SetParamElementInfo( PID_SEQUENCE, 0, "-", 0.f );
		SetParamInfo( PID_STEP, "Step", FF_TYPE_STANDARD, 0.f );
		SetOptionParamInfo( PID_SCALING, "Scaling", 3, (float)SCALING_MASK );
		SetParamElementInfo( PID_SCALING, SCALING_MASK, "Mask", (float)SCALING_MASK );
		SetParamElementInfo( PID_SCALING, SCALING_FILL, "Fill", (float)SCALING_FILL );
		SetParamElementInfo( PID_SCALING, SCALING_STRETCH, "Stretch", (float)SCALING_STRETCH );
		SetRangedParamInfo( PID_LENGTH, "Length", FF_TYPE_STANDARD, 0.25f, 0.01f, 1.f );
		SetRangedParamInfo( PID_OVERLAP, "Overlap", FF_TYPE_STANDARD, 0.f, 0.f, 0.95f );
		SetRangedParamInfo( PID_LOOPS, "Loops", FF_TYPE_INTEGER, 1.f, 1.f, (float)MAX_LOOPS );
		SetParamInfo( PID_FLIP, "Flip Direction", FF_TYPE_BOOLEAN, false );
		SetParamInfo( PID_ODD_EVEN, "Odd/Even", FF_TYPE_BOOLEAN, false );
		SetParamInfo( PID_SWAP, "Swap Directions", FF_TYPE_BOOLEAN, false );
		SetOptionParamInfo( PID_ENVELOPE, "Envelope", 1, 0.f );
		SetParamElementInfo( PID_ENVELOPE, 0, "Gradient", 0.f );
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
		std::string vertex   = std::string( CompositionVertexHeader() ) + vertexShaderBody;
		std::string fragment = std::string( "#version 410 core\n" ) + WipeAlphaGLSL() + fragmentShaderBody;
		if( !shader.Compile( vertex, fragment ) || !copyShader.Compile( copyVertexShader, copyFragmentShader ) || !quad.Initialise() ||
			!mesh.Init( { 2, 2, 2 } ) )
		{
			DeInitGL();
			return FF_FAIL;
		}
		return CFFGLPlugin::InitGL( vp );
	}

	FFResult DeInitGL() override
	{
		shader.FreeGLResources();
		copyShader.FreeGLResources();
		quad.Release();
		mesh.Release();
		return FF_SUCCESS;
	}

	FFResult SetFloatParameter( unsigned int index, float value ) override
	{
		if( index >= PID_COUNT )
			return FF_FAIL;
		values[ index ] = value;
		if( index == PID_SOURCE )
			UpdateVisibility( true );
		return FF_SUCCESS;
	}

	float GetFloatParameter( unsigned int index ) override
	{
		return index < PID_COUNT ? values[ index ] : 0.f;
	}

	const char* GetShortName() override
	{
		static const char* sname = "STWiper";
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
			// Keep what comes in (e.g. a chase above in the chain) and draw the wipes over it.
			ScopedShaderBinding copyBinding( copyShader.GetGLID() );
			ScopedSamplerActivation activateSampler( 0 );
			Scoped2DTextureBinding textureBinding( input.Handle );
			copyShader.Set( "inputTexture", 0 );
			copyShader.Set( "maxUV", inputMax.s, inputMax.t );
			quad.Draw();
		}
		if( doc->slices.empty() || doc->sequences.empty() )
			return FF_SUCCESS;

		BuildMesh( input );
		mesh.Upload( vertices );

		const Sequence& seq = doc->sequences[ Option( values[ PID_SEQUENCE ], (int)doc->sequences.size() ) ];
		const float length  = std::clamp( values[ PID_LENGTH ], 0.01f, 1.f );
		const Envelope* env = ChosenEnvelope( *doc, values[ PID_ENVELOPE ] );
		const auto wipes    = ActiveWipes( (int)seq.steps.size(), values[ PID_STEP ], (int)std::lround( values[ PID_LOOPS ] ), length,
										   values[ PID_OVERLAP ], values[ PID_ODD_EVEN ] > 0.5f );

		ScopedShaderBinding shaderBinding( shader.GetGLID() );
		ScopedSamplerActivation activateSampler( 0 );
		Scoped2DTextureBinding textureBinding( input.Handle );
		const bool colour = Option( values[ PID_SOURCE ], 2 ) == SOURCE_COLOUR;
		shader.Set( "inputTexture", 0 );
		shader.Set( "maxUV", inputMax.s, inputMax.t );
		shader.Set( "wipeLength", length );
		shader.Set( "wipeUseCurve", env ? 1 : 0 );
		if( env )
			glUniform1fv( glGetUniformLocation( shader.GetGLID(), "wipeCurve" ), 256, env->lut.data() );
		shader.Set( "useColour", colour ? 1 : 0 );
		shader.Set( "colour", values[ PID_RED ], values[ PID_GREEN ], values[ PID_BLUE ] );

		// Overlapping video wipes combine by keeping the brightest; colour wipes are laid on top.
		glEnable( GL_BLEND );
		if( colour )
			glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
		else
			glBlendEquation( GL_MAX );
		ScopedVAOBinding vaoBinding( mesh.VAO() );
		for( const ActiveWipe& wipe : wipes )
		{
			shader.Set( "wipeHead", wipe.head );
			for( int slice : seq.steps[ wipe.stepIndex ] )
			{
				const SliceRange& range = ranges[ slice ];
				bool reversed           = range.reversed != ( values[ PID_FLIP ] > 0.5f ) != wipe.odd;
				shader.Set( "wipeAlongY", range.alongY ? 1 : 0 );
				shader.Set( "wipeReversed", reversed ? 1 : 0 );
				glDrawArrays( GL_TRIANGLES, range.first, range.count );
			}
		}
		glBlendEquation( GL_FUNC_ADD );
		glBlendFunc( GL_ONE, GL_ZERO );
		glDisable( GL_BLEND );
		return FF_SUCCESS;
	}

private:
	struct SliceRange
	{
		GLint first;
		GLsizei count;
		bool alongY;
		bool reversed;
	};

	void RefreshMenus( bool raiseEvents )
	{
		const auto sequences = SequenceNames( *doc );
		const auto envelopes = EnvelopeNames( *doc, "Gradient" );
		SetParamElements( PID_SEQUENCE, sequences, Indices( sequences.size() ), raiseEvents );
		SetParamElements( PID_ENVELOPE, envelopes, Indices( envelopes.size() ), raiseEvents );
	}

	// The colour only matters for colour wipes.
	void UpdateVisibility( bool raiseEvents )
	{
		const bool colour = Option( values[ PID_SOURCE ], 2 ) == SOURCE_COLOUR;
		for( int id : { PID_RED, PID_GREEN, PID_BLUE } )
			SetParamVisibility( id, colour, raiseEvents );
	}

	float GetDefault( int index )
	{
		switch( index )
		{
		case PID_SCALING: return (float)SCALING_MASK;
		case PID_LENGTH:  return 0.25f;
		case PID_LOOPS:   return 1.f;
		case PID_RED:
		case PID_GREEN:
		case PID_BLUE:    return 1.f;
		default:          return 0.f;
		}
	}

	// Every slice's triangles with content and slice-local coordinates, and its wipe direction.
	void BuildMesh( const FFGLTextureStruct& input )
	{
		vertices.clear();
		ranges.clear();
		const int scaling         = Option( values[ PID_SCALING ], 3 );
		const float contentAspect = input.Height ? (float)input.Width / (float)input.Height : 1.f;
		const bool swap           = values[ PID_SWAP ] > 0.5f;
		for( const Slice& slice : doc->slices )
		{
			const WipeDirection direction = BaseWipeDirection( *doc, slice, swap );
			ranges.push_back( { (GLint)( vertices.size() / 6 ), (GLsizei)slice.indices.size(), direction.alongY, direction.reversed } );
			for( uint16_t i : slice.indices )
			{
				const Vec2 p     = slice.points[ i ];
				const Vec2 uv    = ContentUV( *doc, slice, p, scaling, contentAspect );
				const Vec2 local = SliceLocal( slice, p );
				vertices.insert( vertices.end(), { p.x, p.y, uv.x, uv.y, local.x, local.y } );
			}
		}
	}

	FFGLShader shader, copyShader;
	FFGLScreenQuad quad;
	FloatVertexBuffer mesh;
	std::vector< float > vertices;
	std::vector< SliceRange > ranges;
	std::shared_ptr< const ChaseDocument > doc;
	uint64_t docVersion = 0;
	float values[ PID_COUNT ];
};

static CFFGLPluginInfo PluginInfo(
	PluginFactory< Wiper >,
	"STWP",       // Plugin unique ID
	"STLT-WIPER", // Plugin name
	2,            // API major version number
	1,            // API minor version number
	1,            // Plugin major version number
	3,            // Plugin minor version number
	FF_EFFECT,
	"Gradient wipes through Advanced Output slices, step by step through a Stilletchaser sequence",
	"Stilletchaser" );
