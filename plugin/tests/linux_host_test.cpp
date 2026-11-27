// A minimal FFGL host for Linux: loads a Stilletchaser plugin (.so) in a headless OpenGL 4.1
// core context (EGL, e.g. Mesa's software renderer), feeds it frames and checks its pixels.
// Covers STLT-SAMPLER and STLT-TILER; the Windows host_test covers the rest.
//   linux_host_test <folder with the plugins> <empty scratch folder>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glew.h>
#include <dlfcn.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <tuple>
#include <string>
#include <vector>
#include <ffgl/FFGL.h>

typedef FFMixed ( *PlugMain )( FFUInt32, FFMixed, FFInstanceID );

static int failures = 0;
static void Check( bool ok, const std::string& what )
{
	std::printf( "%s  %s\n", ok ? "ok  " : "FAIL", what.c_str() );
	if( !ok )
		++failures;
}

static bool CreateContext()
{
	auto getDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress( "eglGetPlatformDisplayEXT" );
	EGLDisplay display = getDisplay ? getDisplay( EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr ) : eglGetDisplay( EGL_DEFAULT_DISPLAY );
	if( display == EGL_NO_DISPLAY || !eglInitialize( display, nullptr, nullptr ) || !eglBindAPI( EGL_OPENGL_API ) )
		return false;
	const EGLint configAttributes[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
	EGLConfig config;
	EGLint count = 0;
	eglChooseConfig( display, configAttributes, &config, 1, &count );
	const EGLint contextAttributes[] = { EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 1,
										 EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
	EGLContext context = eglCreateContext( display, count ? config : nullptr, EGL_NO_CONTEXT, contextAttributes );
	if( context == EGL_NO_CONTEXT || !eglMakeCurrent( display, EGL_NO_SURFACE, EGL_NO_SURFACE, context ) )
		return false;
	glewExperimental = GL_TRUE;
	const GLenum glew = glewInit();
	glGetError();
	std::printf( "GL %s, %s (glew %d)\n", glGetString( GL_VERSION ), glGetString( GL_RENDERER ), (int)glew );
	return true;
}

struct Host
{
	PlugMain plugMain = nullptr;
	FFInstanceID instance = nullptr;
	FFMixed Call( FFUInt32 code, FFMixed input ) { return plugMain( code, input, instance ); }
	void Set( int index, float value )
	{
		SetParameterStruct sp;
		sp.ParameterNumber = index;
		std::memcpy( &sp.NewParameterValue.UIntValue, &value, sizeof( float ) );
		FFMixed in;
		in.PointerValue = &sp;
		Call( FF_SET_PARAMETER, in );
	}
	int Find( const char* name )
	{
		FFMixed in;
		in.UIntValue = 0;
		int count    = (int)plugMain( FF_GET_NUM_PARAMETERS, in, nullptr ).UIntValue;
		for( int i = 0; i < count; ++i )
		{
			in.UIntValue  = i;
			const char* n = (const char*)plugMain( FF_GET_PARAMETER_NAME, in, nullptr ).PointerValue;
			if( n && std::strncmp( n, name, 16 ) == 0 )
				return i;
		}
		std::printf( "no parameter %s\n", name );
		++failures;
		return -1;
	}
	bool Visible( int index )
	{
		FFMixed in;
		in.UIntValue = index;
		return Call( FF_GET_PRAMETER_VISIBILITY, in ).UIntValue != 0;
	}
};

const int W = 100, H = 50;// the composition (and the input and output) in pixels

struct Frame
{
	std::vector< unsigned char > px;
	// The colour at pixel (x, y) with y counted from the top, as on screen.
	const unsigned char* at( int x, int y ) const { return &px[ ( ( H - 1 - y ) * W + x ) * 4 ]; }
};

int main( int argc, char** argv )
{
	if( argc < 3 )
	{
		std::printf( "usage: linux_host_test <plugin folder> <scratch folder>\n" );
		return 2;
	}
	const std::string data = argv[ 2 ];
	setenv( "STLT_DATA_DIR", data.c_str(), 1 );
	// A 100x50 composition: slice A on the left half; slice B, a strip on the right half, turned
	// a quarter so its own top edge is its right side; and C, a small square in the middle of B.
	std::ofstream( data + "/current.stchase" ) << R"({
	  "format": "stilletchaser", "version": 1, "name": "test",
	  "source": { "path": "", "compWidth": 100, "compHeight": 50 },
	  "screens": [], "slices": [
	    { "id": "A", "name": "A", "points": [[0,0],[50,0],[50,50],[0,50]] },
	    { "id": "B", "name": "B", "points": [[100,0],[100,50],[50,50],[50,0]] },
	    { "id": "C", "name": "C", "points": [[70,20],[80,20],[80,30],[70,30]] }
	  ],
	  "sequences": [ { "id": "1", "name": "All", "steps": [ ["A","B","C"] ] } ],
	  "views": [ { "id": "all", "name": "All", "hidden": [] }, { "id": "v", "name": "Only A", "visible": ["A"] } ]
	})";
	std::ofstream( data + "/envelopes.json" ) << R"({"format":"stilletchaser-envelopes","version":1,"envelopes":[]})";

	if( !CreateContext() )
	{
		std::printf( "no OpenGL 4.1 context\n" );
		return 2;
	}

	// The input: the left half red, the right half a blue ramp running 0 to 255 left to right.
	GLuint input, output, fbo;
	std::vector< unsigned char > image( W * H * 4 );
	for( int y = 0; y < H; ++y )
		for( int x = 0; x < W; ++x )
		{
			unsigned char* p = &image[ ( y * W + x ) * 4 ];
			p[ 0 ] = x < 50 ? 255 : 0;
			p[ 1 ] = 0;
			p[ 2 ] = x < 50 ? 0 : (unsigned char)( ( x - 50 ) * 255 / 49 );
			p[ 3 ] = 255;
		}
	glGenTextures( 1, &input );
	glBindTexture( GL_TEXTURE_2D, input );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data() );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glGenTextures( 1, &output );
	glBindTexture( GL_TEXTURE_2D, output );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0 );
	FFGLTextureStruct inputStruct{ (FFUInt32)W, (FFUInt32)H, (FFUInt32)W, (FFUInt32)H, input };
	FFGLTextureStruct* inputs[] = { &inputStruct };
	FFGLViewportStruct viewport{ 0, 0, W, H };

	auto load = [ & ]( const char* file ) {
		const std::string path = std::string( argv[ 1 ] ) + "/" + file;
		void* so               = dlopen( path.c_str(), RTLD_NOW | RTLD_LOCAL );
		Host host;
		host.plugMain = so ? (PlugMain)dlsym( so, "plugMain" ) : nullptr;
		if( !host.plugMain )
		{
			std::printf( "could not load %s: %s\n", path.c_str(), dlerror() );
			std::exit( 2 );
		}
		FFMixed in;
		in.UIntValue = 0;
		Check( host.plugMain( FF_INITIALISE_V2, in, nullptr ).UIntValue == FF_SUCCESS, std::string( file ) + ": initialise" );
		in.PointerValue = &viewport;
		host.instance   = host.plugMain( FF_INSTANTIATE_GL, in, nullptr ).PointerValue;
		Check( host.instance && host.instance != (void*)(uintptr_t)FF_FAIL, std::string( file ) + ": instantiate (shaders compile)" );
		return host;
	};
	auto render = [ & ]( Host& host, float seconds = 0.f ) {
		(void)seconds;
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glViewport( 0, 0, W, H );
		SetBeatinfoStruct beat{ 120.f, 0.f };
		FFMixed b;
		b.PointerValue = &beat;
		host.Call( FF_SET_BEATINFO, b );
		ProcessOpenGLStruct pgl{ 1, inputs, fbo };
		FFMixed p;
		p.PointerValue = &pgl;
		host.Call( FF_PROCESS_OPENGL, p );
		Frame f;
		f.px.resize( W * H * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glReadPixels( 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, f.px.data() );
		return f;
	};
	auto near = []( int value, int expected, int tolerance = 6 ) { return std::abs( value - expected ) <= tolerance; };
	auto rgb  = []( const unsigned char* p ) { return std::to_string( p[ 0 ] ) + "," + std::to_string( p[ 1 ] ) + "," + std::to_string( p[ 2 ] ) + "," + std::to_string( p[ 3 ] ); };

	// ---- STLT-SAMPLER ----
	{
		Host s = load( "STLT-SAMPLER.so" );
		const int sample = s.Find( "Sample" ), colour = s.Find( "Colour" ), red = s.Find( "Red" ), gain = s.Find( "Gain" );
		const int flat = s.Find( "Flat" ), view = s.Find( "View" ), showOriginal = s.Find( "Show Original" );
		Check( !s.Visible( red ), "Sampler: the tint is hidden until Brightness Tint" );

		Frame f = render( s );
		// A is all red: its average is red, everywhere in A.
		Check( near( f.at( 10, 10 )[ 0 ], 255 ) && near( f.at( 10, 10 )[ 2 ], 0 ) && near( f.at( 40, 40 )[ 0 ], 255 ), "Sampler: slice A shows the red under it (" + rgb( f.at( 10, 10 ) ) + ")" );
		// B covers the blue ramp: its average is about half blue, the same at both of its ends (flat colour).
		Check( near( f.at( 55, 25 )[ 2 ], 128, 20 ) && near( f.at( 55, 25 )[ 2 ], f.at( 95, 10 )[ 2 ], 3 ) && f.at( 55, 25 )[ 0 ] < 6,
			   "Sampler: slice B is one flat colour, the average of the ramp (" + rgb( f.at( 55, 25 ) ) + " / " + rgb( f.at( 95, 10 ) ) + ")" );
		// C sits on the ramp at x 70..80, drawn over B: about 75/49*255 = 133 blue... its own average, not B's.
		Check( near( f.at( 75, 25 )[ 2 ], ( 75 - 50 ) * 255 / 49, 12 ), "Sampler: small slice C averages only what is under it (" + rgb( f.at( 75, 25 ) ) + ")" );
		Check( f.at( 10, 10 )[ 3 ] == 255, "Sampler: slices are opaque" );

		s.Set( sample, 1.f );// Slice Centre
		f = render( s );
		Check( near( f.at( 60, 5 )[ 2 ], ( 75 - 50 ) * 255 / 49, 12 ), "Sampler: Slice Centre takes the colour at the middle of B (" + rgb( f.at( 60, 5 ) ) + ")" );
		s.Set( sample, 0.f );

		s.Set( colour, 1.f );// Brightness Tint, in white: grey of the red's brightness
		Check( s.Visible( red ), "Sampler: Brightness Tint shows the tint" );
		f = render( s );
		const int grey = (int)std::lround( 0.2126 * 255 );
		Check( near( f.at( 10, 10 )[ 0 ], grey, 8 ) && near( f.at( 10, 10 )[ 1 ], grey, 8 ) && near( f.at( 10, 10 )[ 2 ], grey, 8 ),
			   "Sampler: Brightness Tint lights A by the red's brightness (" + rgb( f.at( 10, 10 ) ) + ")" );
		s.Set( colour, 0.f );

		s.Set( gain, 0.5f );
		f = render( s );
		Check( near( f.at( 10, 10 )[ 0 ], 128, 8 ), "Sampler: Gain 0.5 halves the colour (" + rgb( f.at( 10, 10 ) ) + ")" );
		s.Set( gain, 1.f );

		s.Set( flat, 0.f );// the video itself inside the slices
		f = render( s );
		Check( near( f.at( 52, 25 )[ 2 ], 5, 8 ) && near( f.at( 98, 25 )[ 2 ], 250, 8 ), "Sampler: Flat 0 shows the video inside the slices (" + rgb( f.at( 52, 25 ) ) + " .. " + rgb( f.at( 98, 25 ) ) + ")" );
		s.Set( flat, 1.f );

		s.Set( view, 1.f );// Only A
		f = render( s );
		Check( near( f.at( 10, 10 )[ 0 ], 255 ) && f.at( 75, 25 )[ 3 ] == 0, "Sampler: a View leaves out the slices it does not include" );
		s.Set( showOriginal, 1.f );
		f = render( s );
		Check( near( f.at( 98, 25 )[ 2 ], 250, 8 ) && f.at( 98, 25 )[ 3 ] == 255, "Sampler: Show Original keeps the video outside the view" );
	}

	// ---- STLT-TILER ----
	{
		Host t = load( "STLT-TILER.so" );
		const int pattern = t.Find( "Pattern" ), phase = t.Find( "Phase" ), tiles = t.Find( "Tiles" ), density = t.Find( "Density" );
		const int gap = t.Find( "Gap" ), source = t.Find( "Source" ), view = t.Find( "View" );
		t.Set( source, 1.f );// solid white tiles: lit = 255, off = transparent
		t.Set( gap, 0.f );
		t.Set( tiles, 8.f );// A and B are squares: 8 x 8 tiles of 6.25 px
		auto lit = []( const Frame& f, int x, int y ) { return f.at( x, y )[ 3 ] > 200; };

		Frame f = render( t );// Checker, Phase 0
		Check( !lit( f, 2, 2 ) && lit( f, 8, 2 ) && lit( f, 2, 8 ) && !lit( f, 8, 8 ), "Tiler: Checker alternates tile by tile" );
		t.Set( phase, 0.6f );
		f = render( t );
		Check( lit( f, 2, 2 ) && !lit( f, 8, 2 ), "Tiler: Checker swaps halfway through Phase" );
		Check( !t.Visible( density ), "Tiler: Density hidden for Checker" );

		t.Set( gap, 0.5f );
		f = render( t );
		Check( !lit( f, 6, 2 ) && lit( f, 3, 3 ), "Tiler: Gap leaves space between tiles" );
		t.Set( gap, 0.f );

		// Scan, narrow: A is on the left, so (Auto) it runs right to left: at Phase 0.06 only its rightmost column.
		t.Set( pattern, 2.f );
		t.Set( density, 0.f );
		t.Set( phase, 0.06f );
		f = render( t );
		Check( t.Visible( density ), "Tiler: Density shown for Scan" );
		Check( lit( f, 47, 10 ) && lit( f, 47, 40 ) && !lit( f, 3, 10 ) && !lit( f, 25, 10 ), "Tiler: Scan starts at A's inner edge (Auto direction)" );
		t.Set( phase, 0.94f );
		f = render( t );
		Check( lit( f, 3, 10 ) && !lit( f, 47, 10 ), "Tiler: ...and ends at its outer edge" );

		// Sparkle: about Density of the tiles, a new set on each step of Phase.
		t.Set( pattern, 3.f );
		t.Set( density, 0.5f );
		t.Set( phase, 0.f );
		f = render( t );
		auto count = [ & ]( const Frame& fr ) {
			int n = 0;
			for( int cy = 0; cy < 8; ++cy )
				for( int cx = 0; cx < 8; ++cx )
					n += lit( fr, (int)( ( cx + 0.5 ) * 6.25 ), (int)( ( cy + 0.5 ) * 6.25 ) );
			return n;
		};
		const int first = count( f );
		std::vector< unsigned char > before = f.px;
		t.Set( phase, 0.5f );
		f = render( t );
		Check( first > 16 && first < 48, "Tiler: Sparkle lights about half the tiles (" + std::to_string( first ) + " of 64)" );
		Check( f.px != before, "Tiler: Sparkle changes with Phase" );

		// Wave: every tile lit, at different brightness along the slice.
		t.Set( pattern, 4.f );
		t.Set( phase, 0.f );
		f = render( t );
		int low = 255, high = 0;
		for( int cx = 0; cx < 8; ++cx )
		{
			const int v = f.at( (int)( ( cx + 0.5 ) * 6.25 ), 3 )[ 0 ];
			low         = std::min( low, v );
			high        = std::max( high, v );
		}
		Check( high > 200 && low < 60, "Tiler: Wave rolls brightness along the slice (" + std::to_string( low ) + ".." + std::to_string( high ) + ")" );

		t.Set( pattern, 0.f );
		t.Set( phase, 0.f );
		t.Set( view, 1.f );// Only A
		f = render( t );
		Check( lit( f, 8, 2 ) && f.at( 75, 25 )[ 3 ] == 0 && f.at( 58, 2 )[ 3 ] == 0, "Tiler: a View leaves out the slices it does not include" );
		t.Set( view, 0.f );
		t.Set( source, 0.f );// the video in the tiles
		f = render( t );
		Check( near( f.at( 8, 2 )[ 0 ], 255 ) && near( f.at( 58, 8 )[ 2 ], ( 58 - 50 ) * 255 / 49, 8 ), "Tiler: Video shows the video in the lit tiles" );
	}

	// ---- Shapes that are not rectangles, and long strips (a second document) ----
	{
		// A triangle T (the top-left half of the left square) over black, with white video beside it in its
		// bounding box; and a strip S, 100 x 30, across the right... the whole width, below.
		std::ofstream( data + "/current.stchase" ) << R"({
		  "format": "stilletchaser", "version": 1, "name": "shapes",
		  "source": { "path": "", "compWidth": 100, "compHeight": 50 },
		  "screens": [], "slices": [
		    { "id": "T", "name": "T", "points": [[0,0],[50,0],[0,50]] },
		    { "id": "S", "name": "S", "points": [[0,10],[100,10],[100,40],[0,40]] }
		  ],
		  "sequences": [ { "id": "1", "name": "All", "steps": [ ["T","S"] ] } ],
		  "views": [ { "id": "all", "name": "All", "hidden": [] }, { "id": "t", "name": "T", "visible": ["T"] }, { "id": "s", "name": "S", "visible": ["S"] } ]
		})";
		for( int y = 0; y < H; ++y )
			for( int x = 0; x < W; ++x )
			{
				unsigned char* p = &image[ ( ( H - 1 - y ) * W + x ) * 4 ];// rows from the bottom, as GL stores them
				const unsigned char v = x + y < 50 ? 0 : 255;
				p[ 0 ] = p[ 1 ] = p[ 2 ] = v;
				p[ 3 ]                   = 255;
			}
		glBindTexture( GL_TEXTURE_2D, input );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, image.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );

		Host s = load( "STLT-SAMPLER.so" );
		s.Set( s.Find( "View" ), 1.f );// T only
		// Each plugin watches the document itself, at most twice a second: wait until each has the new one.
		auto waitFor = [ & ]( Host& host, const std::function< bool( const Frame& ) >& ready ) {
			Frame fr;
			for( int i = 0; i < 30; ++i )
			{
				fr = render( host );
				if( ready( fr ) )
					return std::make_pair( fr, true );
				usleep( 100000 );
			}
			return std::make_pair( fr, false );
		};
		// Ready: T is drawn (opaque at a corner inside it) and the old document's A is gone (outside T).
		auto [ f, sampled ] = waitFor( s, []( const Frame& fr ) { return fr.at( 2, 2 )[ 3 ] == 255 && fr.at( 40, 40 )[ 3 ] == 0; } );
		Check( sampled, "Sampler: picks up the new document" );
		Check( f.at( 10, 10 )[ 3 ] == 255 && f.at( 10, 10 )[ 0 ] < 25,
			   "Sampler: a triangle averages only the black inside it, not the white beside it (" + rgb( f.at( 10, 10 ) ) + ")" );

		Host t = load( "STLT-TILER.so" );
		t.Set( t.Find( "View" ), 2.f );// S only
		t.Set( t.Find( "Source" ), 1.f );
		t.Set( t.Find( "Gap" ), 0.f );
		t.Set( t.Find( "Tiles" ), 8.f );// tiles 12.5 px square: 2.4 rows on the 30 px strip
		auto lit = []( const Frame& fr, int x, int y ) { return fr.at( x, y )[ 3 ] > 200; };
		// Ready: the strip's row-1 tile in column 0 is lit, which only the new document's S can light.
		bool tiled = false;
		std::tie( f, tiled ) = waitFor( t, [ & ]( const Frame& fr ) { return lit( fr, 6, 24 ); } );
		Check( tiled, "Tiler: picks up the new document" );
		// Checker: column 0 is off in row 0 (y 10..22.5) and on in row 1 (y 22.5..35), on again cut short in row 2.
		Check( !lit( f, 6, 12 ) && !lit( f, 6, 21 ) && lit( f, 6, 24 ) && lit( f, 6, 34 ) && !lit( f, 6, 37 ),
			   "Tiler: tiles stay square on a long strip (the last row is cut off, not stretched)" );
	}

	std::printf( "%s\n", failures ? "FAILED" : "all passed" );
	return failures ? 1 : 0;
}
