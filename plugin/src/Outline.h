#pragma once
#include <FFGLSDK.h>
#include <vector>
#include "ChaseDocument.h"
#include "PluginCommon.h"

namespace stilletchaser
{
// How outlines look: STLT-OUTLINER's controls, also used by STLT-CHASER's Outliner module.
struct OutlineStyle
{
	float red = 1.f, green = 1.f, blue = 1.f;
	float width   = 10.f;// Composition pixels, drawn inside the slice.
	float feather = 0.f; // Static outlines: fade this many pixels further in.
	bool animate  = false;
	float phase = 0.f, fan = 0.f, randomise = 0.f;
	float length = 0.25f;// Of the perimeter.
	int count    = 1;
	bool flip = false, oddEven = false;
	const Envelope* envelope = nullptr;// Tail shape; null is a linear taper.
};

// Draws slice outlines over whatever the framebuffer already holds (premultiplied alpha).
// Static outlines are a band inside the edge; animated ones are tapered lines running around it.
class OutlineRenderer
{
public:
	static const int MAX_POINTS = 64;// Corners per slice the shader can follow.
	static const int MAX_LINES  = 16;

	bool InitGL();
	void DeInitGL();

	// `strength` per slice scales its outline (0 = none). With `litTexture`, outlines are also
	// scaled by that texture's alpha, so they only show where it shows something.
	void Draw( const ChaseDocument& doc, const OutlineStyle& style, const std::vector< float >& strength, const FFGLTextureStruct* litTexture );

private:
	void UploadOutline( const ChaseDocument& doc, const Slice& slice );
	int Direction( const OutlineStyle& style, const Slice& slice, Vec2 centre, int shownIndex ) const;

	ffglex::FFGLShader shader;
	FloatVertexBuffer mesh;
	std::vector< float > vertices;
};
}// namespace stilletchaser
