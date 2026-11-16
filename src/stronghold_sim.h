/*
 * stronghold_sim.h — stronghold layout and End portal simulator.
 *
 * Standalone library: given a world seed and a Minecraft version it reports,
 * per stronghold,
 *   - the exact X/Z position of the End portal (the 3x3 `end_portal` area),
 *   - how many eyes generate filled in,
 *   - every generated piece (rooms, corridors, stairs, libraries, crossings)
 *     with its exact X/Z bounding box and chest positions,
 *   - the whole-stronghold planar coverage (true rectangle union).
 *
 * Version coverage
 * ----------------
 * Every version cubiomes knows, from 1.0 through the newest release. Details:
 *
 *   1.0 - 1.12.2   portal X/Z exact; no eye/chest data (see below)
 *   1.13 - 26.3    portal X/Z exact, eyes and chests exact
 *
 * Eyes and chests are produced by cubiomes' `getStrongholdLoot()`, which needs
 * a structure salt config. cubiomes only supplies one for 1.13+, so earlier
 * versions fall back to `getStrongholdPieces()`: the layout (and therefore the
 * portal position) is still exact, but eye rolls are not modelled and are
 * reported as `SSIM_EYES_UNKNOWN` rather than a misleading zero.
 *
 * On Y
 * ----
 * There is no usable world Y. cubiomes anchors every layout at a nominal
 * y = SSIM_NOMINAL_START_Y (64) and does not model the vertical placement
 * vanilla applies afterwards: the adjustment pass in cubiomes'
 * features/stronghold.c only runs on the *failed* attempt loop, never on the
 * attempt that yields the portal room. What is exact is the vertical geometry
 * (deltas between pieces), so every Y field here is named `nominal*`. To get
 * world Y, add (realStartY - SSIM_NOMINAL_START_Y) to all of them.
 */

#ifndef STRONGHOLD_SIM_H_
#define STRONGHOLD_SIM_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of strongholds enumerated (the vanilla cap). */
#define SSIM_MAX_STRONGHOLDS 128

/** Capacity of the piece buffer handed to cubiomes. */
#define SSIM_PIECE_CAPACITY 400

/** Number of End eye frames in a portal room. */
#define SSIM_EYE_COUNT 12

/** Maximum chests per piece (matches cubiomes' Piece.chestPoses[4]). */
#define SSIM_MAX_CHESTS 4

/** `eyes` sentinel: the layout is exact but eye rolls are not modelled. */
#define SSIM_EYES_UNKNOWN (-1)

/** Nominal Y the layout generator anchors the start piece at. */
#define SSIM_NOMINAL_START_Y 64

/** Piece type ids (mirror of cubiomes' SH_* enum). */
enum
{
    SSIM_STRAIGHT = 0,         /* "SHS"   straight corridor                */
    SSIM_PRISON_HALL,          /* "SHPH"  prison hall                      */
    SSIM_LEFT_TURN,            /* "SHLT"  left turn                        */
    SSIM_RIGHT_TURN,           /* "SHRT"  right turn (never before 1.14)    */
    SSIM_ROOM_CROSSING,        /* "SHRC"  room crossing                    */
    SSIM_STRAIGHT_STAIRS_DOWN, /* "SHSSD" straight stairs down             */
    SSIM_STAIRS_DOWN,          /* "SHSD"  stairs down (the start piece)    */
    SSIM_FIVE_CROSSING,        /* "SH5C"  five-way crossing                */
    SSIM_CHEST_CORRIDOR,       /* "SHCC"  chest corridor                   */
    SSIM_LIBRARY,              /* "SHLi"  library                          */
    SSIM_PORTAL_ROOM,          /* "SHPR"  portal room                      */
    SSIM_FILLER_CORRIDOR,      /* "SHFC"  filler corridor (seals a leak)   */
    SSIM_TYPE_COUNT
};

/** One generated stronghold piece, in world-space block coordinates. */
typedef struct
{
    int type;         /* SSIM_* id (== cubiomes SH_* value) */
    const char *code; /* cubiomes' short name, e.g. "SHPR" */
    int rot;          /* 0..3 => 0 / 90 / 180 / 270 degrees */
    int depth;        /* generation depth from the start piece */
    int order;        /* index in the generator's output list */

    /* Inclusive world-space bounding box on the horizontal axes. Exact. */
    int minX, maxX;
    int minZ, maxZ;

    /* Vertical extent relative to SSIM_NOMINAL_START_Y. Exact deltas only. */
    int nominalMinY, nominalMaxY;
    int height; /* nominalMaxY - nominalMinY + 1 */

    /* Chests (0..4). Only modelled when the version has a salt config. */
    int chestCount;
    int chestX[SSIM_MAX_CHESTS];
    int chestZ[SSIM_MAX_CHESTS];
    const char *lootTable[SSIM_MAX_CHESTS]; /* NULL when unknown/predates 1.13 */
} StrongholdSimPiece;

/** Portal + layout summary for one stronghold. */
typedef struct
{
    /* input */
    int mc;
    uint64_t seed;
    int chunkX, chunkZ;

    /* the stronghold start (vanilla puts the staircase at (4,4) in its chunk) */
    int startX, startZ;

    /* portal room */
    int ok;        /* 1 when the portal room was generated */
    int truncated; /* 1 when the piece cap was hit before finding it */
    int pieces;    /* pieces the generator produced */
    int portalRoomIndex;

    /* portal room bounds (Y is nominal) */
    int roomMinX, roomMinZ, roomMaxX, roomMaxZ;
    int roomNominalMinY, roomNominalMaxY;

    /* the 3x3 End portal: inclusive bounds, and its centre */
    int frameMinX, frameMinZ, frameMaxX, frameMaxZ;
    int portalX, portalZ;

    /* eyes: 0..12, or SSIM_EYES_UNKNOWN when the version has no salt config */
    int eyes;
    unsigned eyeMask;
    int eyeFrameX[SSIM_EYE_COUNT];
    int eyeFrameZ[SSIM_EYE_COUNT];

    /* whole-stronghold planar coverage: union of every piece */
    int footprintMinX, footprintMinZ, footprintMaxX, footprintMaxZ;
    int footprintWidth, footprintDepth;
    int footprintArea;
    int footprintChunkMinX, footprintChunkMinZ, footprintChunkMaxX, footprintChunkMaxZ;

    /* per-type piece counts, indexed by SSIM_* */
    int typeCounts[SSIM_TYPE_COUNT];

    /* per-type planar coverage */
    struct
    {
        int count;
        int minX, minZ, maxX, maxZ;
        int area; /* union area of these pieces */
    } typeExtent[SSIM_TYPE_COUNT];
} StrongholdSim;

/* ------------------------------------------------------------------------- */
/* Version support                                                           */
/* ------------------------------------------------------------------------- */

/** Lowest supported version id (MC_1_0_0). */
int ssim_minVersion(void);

/** Newest supported version id. */
int ssim_maxVersion(void);

/** 1 when this build can simulate `mc`. */
int ssim_versionSupported(int mc);

/** 1 when eyes/chests are modelled for `mc` (1.13+). */
int ssim_lootSupported(int mc);

/** Human-readable name for a version id ("1.21.6"), or "?" if unknown. */
const char *ssim_versionName(int mc);

/** Number of distinct versions this build supports (for iteration). */
int ssim_versionCount(void);

/** Version id at `index` in [0, ssim_versionCount()), newest first. */
int ssim_versionAt(int index);

/** English name for a piece type. */
const char *ssim_pieceTypeName(int type);

/** cubiomes' short code for a piece type ("SHPR"). */
const char *ssim_pieceTypeCode(int type);

/* ------------------------------------------------------------------------- */
/* Simulation                                                                */
/* ------------------------------------------------------------------------- */

/**
 * Compute one stronghold from its start chunk.
 *
 * `pieces` may be NULL to skip the piece export. `maxPieces` caps how many
 * pieces are copied out (generation always uses the full SSIM_PIECE_CAPACITY,
 * because the layout is sensitive to that cap).
 *
 * Returns the number of pieces generated, or 0 for an unsupported version.
 */
int ssim_compute(StrongholdSim *out, StrongholdSimPiece *pieces, int maxPieces,
                 int mc, uint64_t seed, int chunkX, int chunkZ);

/**
 * Enumerate strongholds for a seed and compute each one's portal.
 *
 * Returns the number written to `out` (at most SSIM_MAX_STRONGHOLDS).
 */
int ssim_locateAll(StrongholdSim *out, int maxCount, int mc, uint64_t seed);

/** Portal for the stronghold nearest to the origin. Returns 1 on success. */
int ssim_locateFirst(StrongholdSim *out, int mc, uint64_t seed);

/**
 * Initialize the biome generator cache used by stronghold placement.
 *
 * Optional: ssim_locateAll() sets this up itself. Call it directly (and reuse
 * the same version+seed) only when locating many strongholds for one seed, or
 * pass 0 to release it.
 */
int ssim_prepare(int mc, uint64_t seed);

/** Release cached generator state. Safe to call at any time. */
void ssim_release(void);

/**
 * Exact union area of axis-aligned inclusive X/Z rectangles.
 *
 * `rects` holds 4 ints per rectangle: minX, minZ, maxX, maxZ. `scratch` must
 * hold at least 4 * count ints. Returns the covered block count.
 */
long long ssim_unionArea(const int *rects, int count, int *scratch);

/**
 * Render a stronghold's planar coverage as a 1-byte-per-block mask.
 *
 * `bits` must hold footprintWidth * footprintDepth bytes (row-major over z).
 * Returns 1 on success.
 */
int ssim_footprintMask(const StrongholdSimPiece *pieces, int count, const StrongholdSim *portal,
                       uint8_t *bits, long long maxBlocks, int *outWidth, int *outHeight);

#ifdef __cplusplus
}
#endif

#endif /* STRONGHOLD_SIM_H_ */
