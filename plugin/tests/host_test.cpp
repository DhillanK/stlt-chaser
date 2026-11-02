// A minimal FFGL host: loads a Stilletchaser DLL the way Resolume does, feeds it frames
// in an off-screen OpenGL 4.1 context and checks the pixels it draws.
//   host_test <plugin.dll> <empty scratch folder>
#include <windows.h>
#include <GL/glew.h>
#include <GL/wglew.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <ffgl/FFGL.h>

typedef FFMixed( __stdcall* PlugMain )( FFUInt32, FFMixed, FFInstanceID );

static int failures = 0;
static void Check( bool ok, const char* what )
{
	std::printf( "%s  %s\n", ok ? "ok  " : "FAIL", what );
	if( !ok )
		++failures;
}

static bool CreateContext()
{
	WNDCLASSA wc{};
	wc.lpfnWndProc   = DefWindowProcA;
	wc.hInstance     = GetModuleHandleA( nullptr );
	wc.lpszClassName = "StilletchaserHostTest";
	RegisterClassA( &wc );
	HWND window = CreateWindowA( wc.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr );
	HDC dc      = GetDC( window );
	PIXELFORMATDESCRIPTOR pfd{ sizeof( pfd ), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 };
	SetPixelFormat( dc, ChoosePixelFormat( dc, &pfd ), &pfd );
	HGLRC legacy = wglCreateContext( dc );
	wglMakeCurrent( dc, legacy );
	glewExperimental = GL_TRUE;
	if( glewInit() != GLEW_OK || !wglCreateContextAttribsARB )
		return false;
	const int attributes[] = { WGL_CONTEXT_MAJOR_VERSION_ARB, 4, WGL_CONTEXT_MINOR_VERSION_ARB, 1, WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0 };
	HGLRC core = wglCreateContextAttribsARB( dc, nullptr, attributes );
	if( !core )
		return false;
	wglMakeCurrent( dc, core );
	wglDeleteContext( legacy );
	glewExperimental = GL_TRUE;
	glewInit();
	glGetError();// glewInit on a core context leaves an error behind.
	return true;
}

static void WriteFile( const std::string& path, const std::string& text )
{
	std::ofstream( path, std::ios::binary ) << text;
}

struct Host
{
	PlugMain plugMain;
	FFInstanceID instance;

	FFMixed Call( FFUInt32 code, FFMixed input )
	{
		return plugMain( code, input, instance );
	}
	void Set( FFUInt32 index, float value )
	{
		SetParameterStruct sp;
		sp.ParameterNumber = index;
		std::memcpy( &sp.NewParameterValue.UIntValue, &value, sizeof( float ) );
		FFMixed in;
		in.PointerValue = &sp;
		Call( FF_SET_PARAMETER, in );
	}
	int FindParam( const char* name )
	{
		FFMixed in;
		in.UIntValue = 0;
		int count    = (int)plugMain( FF_GET_NUM_PARAMETERS, in, nullptr ).UIntValue;
		for( int i = 0; i < count; ++i )
		{
			in.UIntValue = i;
			const char* n = (const char*)plugMain( FF_GET_PARAMETER_NAME, in, nullptr ).PointerValue;
			if( n && std::strncmp( n, name, 16 ) == 0 )
				return i;
		}
		std::printf( "no parameter %s\n", name );
		return -1;
	}
	bool Visible( int index )
	{
		FFMixed in;
		in.UIntValue = index;
		return Call( FF_GET_PRAMETER_VISIBILITY, in ).UIntValue != 0;
	}
	std::string DisplayName( int index )
	{
		FFMixed in;
		in.UIntValue     = index;
		const char* name = (const char*)Call( FF_GET_PARAM_DISPLAY_NAME, in ).PointerValue;
		return name ? name : "";
	}
	float Get( int index )
	{
		FFMixed in;
		in.UIntValue = index;
		FFMixed v    = Call( FF_GET_PARAMETER, in );
		float value;
		std::memcpy( &value, &v.UIntValue, sizeof( float ) );
		return value;
	}
	std::string ElementName( int param, int element )
	{
		GetParameterElementNameStruct args{ (FFUInt32)param, (FFUInt32)element };
		FFMixed in;
		in.PointerValue = &args;
		const char* name = (const char*)Call( FF_GET_PARAMETER_ELEMENT_NAME, in ).PointerValue;
		return name ? name : "";
	}
};

int main( int argc, char** argv )
{
	if( argc < 3 )
	{
		std::printf( "usage: host_test <plugin.dll> <scratch folder>\n" );
		return 2;
	}
	// The plugin reads its document from %APPDATA%\Stilletchaser.
	std::string appData = argv[ 2 ];
	CreateDirectoryA( ( appData + "\\Stilletchaser" ).c_str(), nullptr );
	_putenv_s( "APPDATA", appData.c_str() );
	// A 100x100 composition split into a left slice A and a right slice B.
	WriteFile( appData + "\\Stilletchaser\\current.stchase", R"({
	  "format": "stilletchaser", "version": 1, "name": "test",
	  "source": { "path": "", "compWidth": 100, "compHeight": 100 },
	  "screens": [ { "id": "s", "name": "Screen 1" } ],
	  "slices": [
	    { "id": "A", "name": "A", "screen": "s", "points": [[0,0],[50,0],[50,100],[0,100]] },
	    { "id": "B", "name": "B", "screen": "s", "points": [[50,0],[100,0],[100,100],[50,100]] }
	  ],
	  "sequences": [
	    { "id": "1", "name": "Chase", "steps": [ ["A"], ["B"] ] },
	    { "id": "2", "name": "Spread", "steps": [ ["A"], [], ["B"] ] }
	  ],
	  "views": [ { "id": "all", "name": "All", "hidden": [] }, { "id": "v", "name": "Only B", "visible": ["B"] } ]
	})" );

	if( !CreateContext() )
	{
		std::printf( "could not create an OpenGL 4.1 context\n" );
		return 2;
	}
	const int size = 100;
	FFGLViewportStruct viewport{ 0, 0, size, size };
	// Loads one plugin from the build folder and makes an instance of it.
	auto load = [ & ]( const char* file ) {
		std::string path = std::string( argv[ 1 ] ) + "\\" + file;
		HMODULE dll      = LoadLibraryA( path.c_str() );
		Host loaded{ dll ? (PlugMain)GetProcAddress( dll, "plugMain" ) : nullptr, nullptr };
		if( !loaded.plugMain )
		{
			std::printf( "could not load %s\n", path.c_str() );
			std::exit( 2 );
		}
		FFMixed in;
		in.UIntValue = 0;
		Check( loaded.plugMain( FF_INITIALISE_V2, in, nullptr ).UIntValue == FF_SUCCESS, ( std::string( file ) + ": initialise" ).c_str() );
		in.PointerValue = &viewport;
		loaded.instance = loaded.plugMain( FF_INSTANTIATE_GL, in, nullptr ).PointerValue;
		Check( loaded.instance != nullptr && loaded.instance != (void*)(uintptr_t)FF_FAIL, ( std::string( file ) + ": instantiate" ).c_str() );
		return loaded;
	};
	Host host = load( "STLT-CHASER.dll" );

	const int driver = host.FindParam( "Driver" ), sequence = host.FindParam( "Sequence" ), step = host.FindParam( "Step" );
	const int sustain = host.FindParam( "Sustain" ), release = host.FindParam( "Release" ), view = host.FindParam( "View" );
	const int delayer = host.FindParam( "Delayer" ), delaySeq = host.FindParam( "Delay Sequence" ), delay = host.FindParam( "Delay" );
	const int unit = host.FindParam( "Set Delay In" ), maxFrames = host.FindParam( "Max Frames" ), reverse = host.FindParam( "Reverse" );
	const int quality = host.FindParam( "Quality" ), scaling = host.FindParam( "Scaling" );

	{
		FFMixed q;
		q.UIntValue = (FFUInt32)host.FindParam( "Max Beats" );
		FFMixed v   = host.Call( FF_GET_PARAMETER, q );
		float beats;
		std::memcpy( &beats, &v.UIntValue, sizeof( float ) );
		Check( beats == 2.f, "defaults above 1 survive (Max Beats starts at 2)" );
	}
	Check( host.ElementName( sequence, 0 ) == "Chase" && host.ElementName( sequence, 1 ) == "Spread", "sequence menu lists the document's sequences" );
	Check( host.ElementName( view, 1 ) == "Only B", "view menu lists the document's views" );
	Check( !host.Visible( delaySeq ), "delayer controls hidden while the Delayer is off" );

	// The sequences are buttons (for MIDI): one per sequence of the document, named after it, the chosen one marked;
	// the menu stays hidden while there are sequences.
	{
		const int first = host.FindParam( "Sequence 1" ), second = host.FindParam( "Sequence 2" ), third = host.FindParam( "Sequence 3" );
		Check( first >= 0 && second == first + 1, "sequence buttons are there" );
		Check( host.Visible( first ) && host.Visible( second ) && !host.Visible( third ), "a button for each of the two sequences" );
		Check( !host.Visible( sequence ), "the sequence menu is hidden: the buttons replace it" );
		Check( host.DisplayName( first ) == "\xe2\x96\xb6 Chase" && host.DisplayName( second ) == "Spread", "buttons named after the sequences, the chosen one marked" );
		host.Set( second, 1.f );
		host.Set( second, 0.f );
		Check( host.Get( sequence ) == 1.f, "pressing a button chooses its sequence" );
		Check( host.DisplayName( first ) == "Chase" && host.DisplayName( second ) == "\xe2\x96\xb6 Spread", "the mark moves to the chosen sequence" );
		host.Set( sequence, 0.f );
		Check( host.DisplayName( first ) == "\xe2\x96\xb6 Chase", "choosing from the menu moves the mark too" );
	}

	// Input texture and output framebuffer.
	GLuint input, output, fbo;
	glGenTextures( 1, &input );
	glBindTexture( GL_TEXTURE_2D, input );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glGenTextures( 1, &output );
	glBindTexture( GL_TEXTURE_2D, output );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0 );

	FFGLTextureStruct inputStruct{ size, size, size, size, input };
	FFGLTextureStruct* inputs[] = { &inputStruct };
	std::vector< unsigned char > pixels( size * size * 4 );

	// Each frame the input is a flat colour whose red channel counts frames, so the
	// output red value tells which frame a slice is showing.
	int frame     = 0;
	bool gradient = false;// Red runs 0..255 from left to right instead of counting frames.
	auto render   = [ & ]() {
		std::vector< unsigned char > colour( size * size * 4 );
		for( int i = 0; i < size * size; ++i )
		{
			colour[ i * 4 + 0 ] = gradient ? (unsigned char)( ( i % size ) * 255 / ( size - 1 ) ) : (unsigned char)( frame % 256 );
			colour[ i * 4 + 1 ] = 200;
			colour[ i * 4 + 2 ] = 0;
			colour[ i * 4 + 3 ] = 255;
		}
		glBindTexture( GL_TEXTURE_2D, input );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, colour.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glViewport( 0, 0, size, size );
		SetBeatinfoStruct beat{ 120.f, 0.f };
		FFMixed b;
		b.PointerValue = &beat;
		host.Call( FF_SET_BEATINFO, b );
		ProcessOpenGLStruct pgl{ 1, inputs, fbo };
		FFMixed p;
		p.PointerValue = &pgl;
		FFResult result = host.Call( FF_PROCESS_OPENGL, p ).UIntValue;
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glReadPixels( 0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
		++frame;
		return result;
	};
	// Pixel in GL coordinates (y up); slices are symmetric vertically so y does not matter.
	auto px = [ & ]( int x, int channel ) { return (int)pixels[ ( 50 * size + x ) * 4 + channel ]; };

	host.Set( scaling, 0.f );// Mask
	host.Set( sustain, 1.f );
	host.Set( release, 1.f );
	host.Set( step, 0.25f );
	Check( render() == FF_SUCCESS, "process frame" );
	Check( px( 25, 1 ) == 200 && px( 75, 1 ) == 0 && px( 75, 3 ) == 0, "step 1 lights A only" );
	host.Set( step, 0.75f );
	render();
	Check( px( 25, 1 ) == 0 && px( 75, 1 ) == 200, "step 2 lights B only" );

	host.Set( sustain, 0.f );
	host.Set( release, 1.f );
	host.Set( step, 0.5f + 0.25f );// halfway through step 2: linear fade at 50%
	render();
	Check( px( 75, 1 ) > 90 && px( 75, 1 ) < 110, "sustain 0 / release 1 fades across the step" );

	host.Set( view, 1.f );
	host.Set( step, 0.25f );
	host.Set( sustain, 1.f );
	render();
	Check( px( 25, 1 ) == 0, "view 'Only B' leaves A dark" );
	host.Set( view, 0.f );

	host.Set( driver, 1.f );
	Check( !host.Visible( step ), "Step hidden with Driver None" );
	render();
	Check( px( 25, 1 ) == 200 && px( 75, 1 ) == 200, "Driver None lights every slice" );

	// Delayer: sequence "Spread" puts A in the first step (no delay) and B in the last (full delay).
	host.Set( delayer, 1.f );
	Check( host.Visible( delaySeq ) && host.Visible( maxFrames ) == false, "delayer controls shown, Max Frames hidden while unit is Beats" );
	host.Set( unit, 0.f );// Frames
	Check( host.Visible( maxFrames ), "Max Frames shown for unit Frames" );
	host.Set( delaySeq, 1.f );
	host.Set( maxFrames, 10.f );
	host.Set( delay, 1.f );
	host.Set( quality, 1.f );
	for( int i = 0; i < 20; ++i )
		render();
	int newest = frame - 1;
	std::printf( "      left red %d, right red %d, newest frame red %d\n", px( 25, 0 ), px( 75, 0 ), newest );
	Check( px( 25, 0 ) == newest, "Delayer: first-step slice shows the current frame" );
	Check( px( 75, 0 ) == newest - 10, "Delayer: last-step slice shows the frame from 10 frames ago" );

	host.Set( delay, 0.5f );
	render();
	newest = frame - 1;
	Check( px( 75, 0 ) == newest - 5, "Delay 50% halves the delay" );

	host.Set( delay, 1.f );
	host.Set( reverse, 1.f );
	render();
	newest = frame - 1;
	Check( px( 25, 0 ) == newest - 10 && px( 75, 0 ) == newest, "Reverse swaps which slice is delayed" );

	// Delay in seconds at reduced quality, with real time passing between frames: each slice must
	// pick the frame from that long ago, and the delay must stay steady (no history resets).
	const int maxSeconds = host.FindParam( "Max Seconds" );
	host.Set( reverse, 0.f );
	host.Set( unit, 1.f );// Seconds
	host.Set( maxSeconds, 0.25f );
	host.Set( delay, 1.f );
	host.Set( quality, 0.5f );
	std::vector< double > sentAt( 256, 0.0 );
	std::vector< int > shown;
	const auto start = std::chrono::steady_clock::now();
	auto seconds     = [ & ]() { return std::chrono::duration< double >( std::chrono::steady_clock::now() - start ).count(); };
	for( int i = 0; i < 70; ++i )
	{
		Sleep( 12 + ( i % 3 ) * 4 );// An uneven frame rate, like a busy show machine.
		sentAt[ frame % 256 ] = seconds();
		render();
		shown.push_back( px( 75, 0 ) );
	}
	bool steady = true;
	for( size_t i = 40; i < shown.size(); ++i )
		if( shown[ i ] < shown[ i - 1 ] || shown[ i ] - shown[ i - 1 ] > 3 )
			steady = false;
	const double age = sentAt[ frame - 1 ] - sentAt[ shown.back() ];
	std::printf( "      newest frame %d, delayed slice shows frame %d, which is %.3f s older\n", frame - 1, shown.back(), age );
	Check( steady, "Seconds: the delayed slice moves forward smoothly, one frame at a time" );
	Check( age > 0.20 && age < 0.30, "Seconds: the delayed slice shows the frame from 0.25 s ago" );
	Check( px( 25, 0 ) == frame - 1, "Seconds: the undelayed slice shows the current frame" );

	// Scaling, with a left-to-right gradient as content. The middle of slice B (x = 75) shows:
	// Mask: the content under it (x = 75); Stretch / Fill: the middle of the whole content (x = 50).
	host.Set( delayer, 0.f );
	gradient = true;
	host.Set( scaling, 0.f );
	render();
	int mask = px( 75, 0 );
	host.Set( scaling, 2.f );
	render();
	int stretch = px( 75, 0 );
	host.Set( scaling, 1.f );
	render();
	int fill = px( 75, 0 ), fillEdge = px( 51, 0 );
	std::printf( "      red at B's middle: mask %d, stretch %d, fill %d (fill at B's left edge %d)\n", mask, stretch, fill, fillEdge );
	Check( mask > 180 && mask < 200, "Mask keeps the content in place" );
	Check( stretch > 118 && stretch < 138, "Stretch maps the whole content into the slice" );
	// B is 50x100 (portrait) and the content square: Fill crops the content's sides, so B's left edge shows x ~25%.
	Check( fill > 118 && fill < 138 && fillEdge > 55 && fillEdge < 75, "Fill crops the content to the slice's shape" );

	// Wiper module: the chase lights A (step 1) while a wipe runs through B (step 2 of the wipe
	// sequence). On the composition both must show at once.
	{
		gradient = false;
		host.Set( scaling, 0.f );
		host.Set( driver, 0.f );
		host.Set( sustain, 1.f );
		host.Set( release, 1.f );
		host.Set( step, 0.25f );
		const int wiper = host.FindParam( "Wiper" ), wipeSeq = host.FindParam( "Wipe Sequence" ), wipeStep = host.FindParam( "Wipe Step" );
		const int wipeLength = host.FindParam( "Wipe Length" ), wipeSource = host.FindParam( "Wipe Source" ), wipeGreen = host.FindParam( "Wipe Green" );
		const int wipeBlue = host.FindParam( "Wipe Blue" );
		auto at = [ & ]( int x, int row, int channel ) { return (int)pixels[ ( row * size + x ) * 4 + channel ]; };
		Check( !host.Visible( wipeSeq ), "Wiper module hidden while off" );
		host.Set( wiper, 1.f );
		Check( host.Visible( wipeSeq ) && !host.Visible( wipeGreen ), "Wiper module shown; colour hidden while Source is Video" );
		host.Set( wipeSeq, 0.f );
		host.Set( wipeLength, 0.5f );
		host.Set( wipeStep, 0.75f );
		render();
		std::printf( "      chase + wipe: A middle green %d, B upper %d, B middle %d, B top %d\n", at( 25, 50, 1 ), at( 75, 70, 1 ), at( 75, 50, 1 ), at( 75, 95, 1 ) );
		Check( at( 25, 50, 1 ) == 200, "Wiper module: the chase stays fully lit" );
		Check( at( 75, 70, 1 ) > 160 && at( 75, 50, 1 ) > 80 && at( 75, 95, 1 ) == 0, "Wiper module: the wipe runs through another slice at the same time" );
		host.Set( wipeSource, 1.f );
		host.Set( wipeGreen, 0.f );
		host.Set( wipeBlue, 0.f );
		Check( host.Visible( wipeGreen ), "Wiper module: colour shown for Solid Color" );
		render();
		std::printf( "      colour wipe: B upper rgb %d %d %d\n", at( 75, 70, 0 ), at( 75, 70, 1 ), at( 75, 70, 2 ) );
		Check( at( 75, 70, 0 ) > 160 && at( 75, 70, 1 ) == 0 && at( 25, 50, 1 ) == 200, "Wiper module: Solid Color wipes in red next to the chase" );

		// Outliner module, white 10 px outlines. Lit Slices: A (chase) and B (wipe) while the wipe runs.
		const int outliner = host.FindParam( "Outliner" ), oTarget = host.FindParam( "Outline Target" ), oOnly = host.FindParam( "Outlines Only" );
		const int oWidth = host.FindParam( "Outline Width" ), oPhase = host.FindParam( "Outline Phase" );
		Check( !host.Visible( oTarget ), "Outliner module hidden while off" );
		host.Set( outliner, 1.f );
		Check( host.Visible( oTarget ) && host.Visible( oWidth ) && !host.Visible( oPhase ), "Outliner module shown; animation controls hidden until Animate" );
		render();
		std::printf( "      outlines: A edge rgba %d %d %d %d, A middle green %d, B edge %d %d %d %d\n", at( 2, 50, 0 ), at( 2, 50, 1 ), at( 2, 50, 2 ), at( 2, 50, 3 ),
					 at( 25, 50, 1 ), at( 97, 50, 0 ), at( 97, 50, 1 ), at( 97, 50, 2 ), at( 97, 50, 3 ) );
		Check( at( 2, 50, 0 ) == 255 && at( 2, 50, 1 ) == 255 && at( 25, 50, 1 ) == 200, "Outliner module: the chased slice is outlined over its content" );
		Check( at( 97, 50, 0 ) == 255 && at( 97, 50, 1 ) == 255 && at( 97, 50, 3 ) == 255, "Outliner module: Lit Slices also outlines the slice a wipe runs through" );
		host.Set( wiper, 0.f );
		render();
		Check( at( 2, 50, 3 ) == 255 && at( 97, 50, 3 ) == 0, "Outliner module: Lit Slices leaves unlit slices alone" );
		host.Set( oTarget, 0.f );
		render();
		Check( at( 97, 50, 3 ) == 255 && at( 75, 50, 3 ) == 0, "Outliner module: All Slices outlines every slice of the view" );
		host.Set( oOnly, 1.f );
		render();
		Check( at( 25, 50, 3 ) == 0 && at( 2, 50, 3 ) == 255, "Outliner module: Outlines Only hides the content" );
		host.Set( oOnly, 0.f );
		host.Set( oTarget, 1.f );
		host.Set( outliner, 0.f );
		render();
		Check( at( 2, 50, 1 ) == 200, "Outliner module off: no outlines" );
	}

	FFMixed none;
	none.UIntValue = 0;
	host.Call( FF_DEINSTANTIATE_GL, none );

	// ---- STLT-WIPER ----
	// Slices A and B are 50x100 (portrait), so they wipe bottom to top. With Length 0.5 and
	// Step a quarter in (half of step 1's wipe), the band's head is at 75% of A's height.
	gradient = false;
	host     = load( "STLT-WIPER.dll" );
	{
		const int wStep = host.FindParam( "Step" ), wLength = host.FindParam( "Length" ), wFlip = host.FindParam( "Flip Direction" );
		const int wOverlap = host.FindParam( "Overlap" );
		Check( host.ElementName( host.FindParam( "Sequence" ), 1 ) == "Spread", "Wiper: sequence menu from the document" );
		// Row in GL coordinates (y up): row 70 is 30% down from the top of the slice.
		auto at = [ & ]( int x, int row ) { return (int)pixels[ ( row * size + x ) * 4 + 1 ]; };
		host.Set( wLength, 0.5f );
		host.Set( wStep, 0.25f );
		render();
		std::printf( "      A: top %d, upper %d, middle %d, bottom %d; B middle %d\n", at( 25, 95 ), at( 25, 70 ), at( 25, 50 ), at( 25, 5 ), at( 75, 50 ) );
		Check( at( 25, 95 ) == 0 && at( 25, 5 ) == 0, "Wiper: nothing ahead of or far behind the band" );
		Check( at( 25, 70 ) > 160 && at( 25, 50 ) > 80 && at( 25, 50 ) < 125, "Wiper: bright at the leading edge, fading behind it" );
		Check( at( 75, 50 ) == 0, "Wiper: slices of other steps stay dark" );
		host.Set( wFlip, 1.f );
		render();
		Check( at( 25, 70 ) < 40, "Wiper: Flip Direction wipes top to bottom" );
		host.Set( wFlip, 0.f );
		host.Set( wStep, 0.75f );
		render();
		const int aTopNoOverlap = at( 25, 95 );
		Check( at( 75, 50 ) > 80 && at( 25, 50 ) == 0, "Wiper: step 2 wipes B" );
		host.Set( wOverlap, 0.5f );
		render();
		Check( aTopNoOverlap == 0 && at( 25, 95 ) > 0, "Wiper: with Overlap the previous wipe is still finishing" );

		// Solid colour over the original: B shows the red wipe blended over the incoming green.
		host.Set( wOverlap, 0.f );
		host.Set( host.FindParam( "Source" ), 1.f );
		host.Set( host.FindParam( "Green" ), 0.f );
		host.Set( host.FindParam( "Blue" ), 0.f );
		host.Set( host.FindParam( "Show Original" ), 1.f );
		render();
		auto rgb = [ & ]( int x, int row, int channel ) { return (int)pixels[ ( row * size + x ) * 4 + channel ]; };
		std::printf( "      colour over original: A middle rgb %d %d, B upper rgb %d %d\n", rgb( 25, 50, 0 ), rgb( 25, 50, 1 ), rgb( 75, 70, 0 ), rgb( 75, 70, 1 ) );
		Check( rgb( 25, 50, 1 ) == 200 && rgb( 75, 70, 0 ) > 160 && rgb( 75, 70, 1 ) < 60, "Wiper: Solid Color with Show Original lays red wipes over the video" );
		host.Call( FF_DEINSTANTIATE_GL, none );
	}

	// ---- STLT-OUTLINER ----
	// Width 10 px inside a 100 px composition: A's left edge (x 0..9) is lit, its middle is not.
	host = load( "STLT-OUTLINER.dll" );
	{
		const int oShow = host.FindParam( "Show Original" ), oAnimate = host.FindParam( "Animate" ), oPhase = host.FindParam( "Phase" );
		const int oFeather = host.FindParam( "Feather" ), oLength = host.FindParam( "Length" ), oRed = host.FindParam( "Red" );
		auto at = [ & ]( int x, int row, int channel ) { return (int)pixels[ ( row * size + x ) * 4 + channel ]; };
		Check( host.Visible( oFeather ) && !host.Visible( oPhase ), "Outliner: animation controls hidden until Animate" );
		host.Set( oShow, 0.f );// Start from outlines alone; Show Original is on by default.
		render();
		if( at( 2, 50, 3 ) != 255 )
		{
			FFMixed q;
			q.UIntValue = (FFUInt32)host.FindParam( "Width" );
			FFMixed v   = host.Call( FF_GET_PARAMETER, q );
			float width;
			std::memcpy( &width, &v.UIntValue, sizeof( float ) );
			std::printf( "      debug: pixel at A's edge rgba %d %d %d %d, Width parameter %.2f, GL error 0x%x\n", at( 2, 50, 0 ), at( 2, 50, 1 ), at( 2, 50, 2 ),
						 at( 2, 50, 3 ), width, glGetError() );
		}
		Check( at( 2, 50, 0 ) == 255 && at( 2, 50, 3 ) == 255, "Outliner: white outline along the slice edge" );
		Check( at( 25, 50, 3 ) == 0, "Outliner: nothing inside the slice without Show Original" );
		Check( at( 47, 50, 3 ) == 255 && at( 53, 50, 3 ) == 255, "Outliner: neighbouring slices each get their own outline" );
		host.Set( oRed, 0.f );
		render();
		Check( at( 2, 50, 0 ) == 0 && at( 2, 50, 1 ) == 255, "Outliner: Color sets the outline colour" );
		host.Set( oShow, 1.f );
		render();
		Check( at( 25, 50, 1 ) == 200 && at( 2, 50, 1 ) == 255, "Outliner: Show Original draws the outline over the input" );
		host.Set( oShow, 0.f );

		// Animated: one line a quarter of the perimeter long. Phase 0 puts its head at A's
		// top-left corner with its tail running back down the left edge.
		host.Set( oAnimate, 1.f );
		Check( !host.Visible( oFeather ) && host.Visible( oPhase ), "Outliner: Animate shows the animation controls" );
		host.Set( oLength, 0.25f );
		host.Set( oPhase, 0.f );
		render();
		std::printf( "      left edge: near the head %d, near the tail %d, past the tail %d\n", at( 2, 90, 3 ), at( 1, 50, 3 ), at( 1, 10, 3 ) );
		Check( at( 2, 90, 3 ) > 0 && at( 1, 50, 3 ) > 0 && at( 1, 10, 3 ) == 0, "Outliner: a tapered line runs up the left edge" );
		host.Set( oPhase, 0.5f );
		render();
		Check( at( 2, 90, 3 ) == 0 && at( 97, 10, 3 ) > 0, "Outliner: Phase moves the line around the slice" );

		// In a chain: Stilletchaser lights slice A, its output feeds this Outliner.
		host.Set( oAnimate, 0.f );
		host.Set( oRed, 1.f );
		Host chaser = load( "STLT-CHASER.dll" );
		Host outliner = host;
		host = chaser;
		host.Set( host.FindParam( "Scaling" ), 0.f );
		host.Set( host.FindParam( "Sustain" ), 1.f );
		host.Set( host.FindParam( "Release" ), 1.f );
		host.Set( host.FindParam( "Step" ), 0.25f );
		render();
		GLuint chained;
		glGenTextures( 1, &chained );
		glBindTexture( GL_TEXTURE_2D, chained );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );// No mipmaps: the default filter would read black.
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glBindTexture( GL_TEXTURE_2D, 0 );
		host = outliner;
		auto renderChained = [ & ]() {
			glBindFramebuffer( GL_FRAMEBUFFER, fbo );
			glViewport( 0, 0, size, size );
			FFGLTextureStruct chainedStruct{ (FFUInt32)size, (FFUInt32)size, (FFUInt32)size, (FFUInt32)size, chained };
			FFGLTextureStruct* chainedInputs[] = { &chainedStruct };
			ProcessOpenGLStruct pgl{ 1, chainedInputs, fbo };
			FFMixed p;
			p.PointerValue = &pgl;
			host.Call( FF_PROCESS_OPENGL, p );
			glReadPixels( 0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
		};
		host.Set( oShow, 1.f );
		renderChained();
		std::printf( "      chain: A middle rgba %d %d %d %d, A edge %d %d %d %d, B edge alpha %d\n", at( 25, 50, 0 ), at( 25, 50, 1 ), at( 25, 50, 2 ),
					 at( 25, 50, 3 ), at( 2, 50, 0 ), at( 2, 50, 1 ), at( 2, 50, 2 ), at( 2, 50, 3 ), at( 97, 50, 3 ) );
		Check( at( 25, 50, 1 ) == 200 && at( 2, 50, 0 ) == 255, "Chaser > Outliner: the chase stays visible with its outlines" );
		Check( at( 97, 50, 3 ) == 255, "Chaser > Outliner: without Lit Slices Only every slice is outlined" );
		host.Set( host.FindParam( "Lit Slices Only" ), 1.f );
		renderChained();
		Check( at( 2, 50, 3 ) == 255 && at( 97, 50, 3 ) == 0 && at( 75, 50, 3 ) == 0, "Chaser > Outliner: Lit Slices Only outlines just the lit slice" );
		host.Call( FF_DEINSTANTIATE_GL, none );
		chaser.Call( FF_DEINSTANTIATE_GL, none );
	}

	std::printf( "%s\n", failures ? "FAILED" : "all passed" );
	return failures;
}
