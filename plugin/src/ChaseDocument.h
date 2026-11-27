#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The editor's working copy (current.stchase) and shared envelopes (envelopes.json),
// in the shape the plugins need for rendering. See FORMAT.md.
namespace stilletchaser
{
struct Vec2
{
	float x, y;
};

// Which way wipes run through a slice and outlines run around it, set per slice in the editor.
// Directions are the slice's own: Up is towards its top edge (its first two points) whatever its rotation.
enum SliceWipe
{
	WIPE_AUTO,
	WIPE_UP,
	WIPE_RIGHT,
	WIPE_DOWN,
	WIPE_LEFT,
	WIPE_MIRROR_V,// from the middle out to the top and bottom edges
	WIPE_MIRROR_H,// from the middle out to the left and right edges
};

enum SliceOutline
{
	OUTLINE_AUTO,
	OUTLINE_CW,
	OUTLINE_CCW,
	OUTLINE_MIRRORED,// both ways at once
};

struct Slice
{
	std::string id;
	std::string name;
	std::vector< Vec2 > points;     // Composition space, normalised 0..1, y down.
	std::vector< uint16_t > indices;// Triangulation of `points`.
	// Frame the content is stretched or filled into: origin plus the slice's own x and y edges.
	// Rotated rectangles keep their rotation; other shapes use their bounding box.
	Vec2 origin, axisX, axisY;
	SliceWipe wipe       = WIPE_AUTO;
	SliceOutline outline = OUTLINE_AUTO;
};

// How a wipe runs through a slice: along its x or y edge, from which end, or from the middle out.
struct WipeDirection
{
	bool alongY;  // Along the slice's own y edge rather than its x edge.
	bool reversed;// From the far edge back (local y runs down the slice); with mirror: from the edges in.
	bool mirror = false;
};

// The slice's own choice, or `automatic` when it has none.
inline WipeDirection ResolveWipe( SliceWipe wipe, WipeDirection automatic )
{
	switch( wipe )
	{
	case WIPE_UP:       return { true, true };
	case WIPE_DOWN:     return { true, false };
	case WIPE_LEFT:     return { false, true };
	case WIPE_RIGHT:    return { false, false };
	case WIPE_MIRROR_V: return { true, false, true };
	case WIPE_MIRROR_H: return { false, false, true };
	default:            return automatic;
	}
}

SliceWipe ParseSliceWipe( const std::string& name );
SliceOutline ParseSliceOutline( const std::string& name );

struct Sequence
{
	std::string id;
	std::string name;
	std::vector< std::vector< int > > steps;// Indices into ChaseDocument::slices.
};

struct View
{
	std::string id;
	std::string name;
	std::vector< bool > includes;// One flag per slice.
};

struct Envelope
{
	std::string id;
	std::string name;
	std::array< float, 256 > lut;
};

struct ChaseDocument
{
	float compWidth = 0.f, compHeight = 0.f;
	std::vector< Slice > slices;
	std::vector< Sequence > sequences;
	std::vector< View > views;// The first is always "All".
	std::vector< Envelope > envelopes;
	std::string error;// Why the document could not be read, if it could not.
};

std::string DataDirectory();

// Parses the two files; missing or broken files give an empty document with `error` set.
std::shared_ptr< const ChaseDocument > LoadChaseDocument( const std::string& documentPath, const std::string& envelopesPath );

// One shared copy of the editor's working document, reloaded when the editor saves.
// Cheap to call every frame: it checks the files at most twice a second.
class DocumentWatcher
{
public:
	static DocumentWatcher& Instance();
	std::shared_ptr< const ChaseDocument > Current();
	// Increases every time a new version is loaded, so instances can refresh their menus.
	uint64_t Version() const
	{
		return version;
	}

private:
	DocumentWatcher();
	void Reload();

	std::string documentPath, envelopesPath;
	std::shared_ptr< const ChaseDocument > document;
	int64_t documentStamp = -1, envelopesStamp = -1;
	uint64_t version   = 0;
	uint64_t lastCheck = 0;
};

// Opacity curve helpers shared by the drivers.
float SampleLut( const std::array< float, 256 >& lut, float x );
float SustainRelease( float sustain, float release, float x );
}// namespace stilletchaser
