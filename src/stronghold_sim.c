/*
 * stronghold_sim.c — stronghold layout and End portal simulator.
 *
 * See stronghold_sim.h for the public contract, including the two caveats that
 * matter most: eye/chest data needs 1.13+, and every Y value is nominal.
 *
 * This file is a driver around cubiomes' own piece generator
 * (features/stronghold.c), which reproduces the vanilla StrongholdPieces
 * layout bit for bit; all it does is feed that generator and translate the
 * portal room's local geometry into world coordinates.
 */

#include "stronghold_sim.h"

#include <stdlib.h>
#include <string.h>

#include <cubiomes/biomes.h>
#include <cubiomes/finders.h>
#include <cubiomes/generator.h>
#include <cubiomes/util.h>
#include <cubiomes/features/piece.h>
#include <cubiomes/features/stronghold.h>

/* ------------------------------------------------------------------------- */
/* Portal room local geometry                                                */
/* ------------------------------------------------------------------------- */

/* The twelve eye frames, straight from cubiomes' features/stronghold.c
 * (`eye_positions`, in the same index order as `additionalData`'s bits). The
 * nine `end_portal` blocks they enclose are the 3x3 area inside that ring. */
static const Pos EYE_LOCAL[SSIM_EYE_COUNT] = {
    {4, 8}, {5, 8}, {6, 8},
    {4, 12}, {5, 12}, {6, 12},
    {3, 9}, {3, 10}, {3, 11},
    {7, 9}, {7, 10}, {7, 11},
};

/* 3x3 block area: one corner, the opposite corner, and the centre. */
static const Pos PORTAL_CORNER_A = {4, 9};
static const Pos PORTAL_CORNER_B = {6, 11};
static const Pos PORTAL_CENTER = {5, 10};

static const char *const PIECE_NAMES[SSIM_TYPE_COUNT] = {
    "Straight corridor", "Prison hall", "Left turn", "Right turn",
    "Room crossing", "Straight stairs down", "Stairs down", "Five-way crossing",
    "Chest corridor", "Library", "Portal room", "Filler corridor",
};

static const char *const PIECE_CODES[SSIM_TYPE_COUNT] = {
    "SHS", "SHPH", "SHLT", "SHRT", "SHRC", "SHSSD", "SHSD", "SH5C", "SHCC",
    "SHLi", "SHPR", "SHFC",
};

/* ------------------------------------------------------------------------- */
/* Version tables                                                            */
/* ------------------------------------------------------------------------- */

/* Every version this build can address, newest first. Built once on demand so
 * the list stays in sync with cubiomes' enum instead of being retyped. */
static int g_versions[64];
static int g_versionCount = -1;

static void buildVersionTable(void)
{
    if (g_versionCount >= 0) return;

    static const int ids[] = {
        MC_26_3, MC_26_2, MC_26_1,
        MC_1_21_11, MC_1_21_9, MC_1_21_6, MC_1_21_5, MC_1_21_4, MC_1_21_3,
        MC_1_21_1, MC_1_20_6, MC_1_19_4, MC_1_19_2, MC_1_18_2, MC_1_17_1,
        MC_1_16_5, MC_1_16_1, MC_1_15_2, MC_1_14_4, MC_1_13_2, MC_1_12_2,
        MC_1_11_2, MC_1_10_2, MC_1_9_4, MC_1_8_9, MC_1_7_10, MC_1_6_4,
        MC_1_5_2, MC_1_4_7, MC_1_3_2, MC_1_2_5, MC_1_1_0, MC_1_0_0,
    };
    const int n = (int)(sizeof(ids) / sizeof(ids[0]));
    for (int i = 0; i < n; i++) g_versions[i] = ids[i];
    g_versionCount = n;
}

int ssim_minVersion(void)
{
    /* Strongholds were added in the Beta 1.9 pre-releases, released as 1.0.
     * Beta 1.8 has none, even though cubiomes' iterator would still return
     * three positions for it. */
    return MC_1_0_0;
}

int ssim_maxVersion(void)
{
    return MC_NEWEST;
}

int ssim_versionSupported(int mc)
{
    return mc >= ssim_minVersion() && mc <= ssim_maxVersion();
}

int ssim_lootSupported(int mc)
{
    /* Eyes and chests come from getStrongholdLoot(), which needs a structure
     * salt config; cubiomes only provides one for 1.13+. */
    return mc >= MC_1_13 && ssim_versionSupported(mc);
}

const char *ssim_versionName(int mc)
{
    const char *name = mc2str(mc);
    return (name && name[0] != '?') ? name : "?";
}

int ssim_versionCount(void)
{
    buildVersionTable();
    return g_versionCount;
}

int ssim_versionAt(int index)
{
    buildVersionTable();
    if (index < 0 || index >= g_versionCount) return MC_UNDEF;
    return g_versions[index];
}

const char *ssim_pieceTypeName(int type)
{
    if (type < 0 || type >= SSIM_TYPE_COUNT) return "Unknown";
    return PIECE_NAMES[type];
}

const char *ssim_pieceTypeCode(int type)
{
    if (type < 0 || type >= SSIM_TYPE_COUNT) return "????";
    return PIECE_CODES[type];
}

/* ------------------------------------------------------------------------- */
/* Generator cache                                                           */
/* ------------------------------------------------------------------------- */

static Generator g_gen;
static int g_genReady = 0;
static int g_genMc = -1;
static uint64_t g_genSeed = 0;

int ssim_prepare(int mc, uint64_t seed)
{
    if (!ssim_versionSupported(mc)) return 0;
    if (g_genReady && g_genMc == mc && g_genSeed == seed) return 1;
    setupGenerator(&g_gen, mc, 0);
    applySeed(&g_gen, DIM_OVERWORLD, seed);
    g_genReady = 1;
    g_genMc = mc;
    g_genSeed = seed;
    return 1;
}

void ssim_release(void)
{
    g_genReady = 0;
    g_genMc = -1;
    g_genSeed = 0;
}

/* ------------------------------------------------------------------------- */
/* Rectangle union                                                           */
/* ------------------------------------------------------------------------- */

static int cmpIntAsc(const void *a, const void *b)
{
    const int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

long long ssim_unionArea(const int *rects, int count, int *scratch)
{
    if (!rects || !scratch || count <= 0) return 0;

    /* Coordinate-compress X into slabs, then merge the Z intervals covered in
     * each slab. Exact for axis-aligned inclusive integer rectangles. */
    int *xs = scratch;
    int nx = 0;
    for (int i = 0; i < count; i++)
    {
        const int minX = rects[4 * i + 0];
        const int maxX = rects[4 * i + 2];
        if (minX > maxX) continue;
        xs[nx++] = minX;
        xs[nx++] = maxX + 1; /* half-open upper edge */
    }
    if (nx == 0) return 0;
    qsort(xs, (size_t)nx, sizeof(int), cmpIntAsc);

    int uniq = 0;
    for (int i = 0; i < nx; i++)
        if (i == 0 || xs[i] != xs[i - 1])
            xs[uniq++] = xs[i];

    int *zspan = scratch + 2 * count;
    long long total = 0;
    for (int k = 0; k + 1 < uniq; k++)
    {
        const int x0 = xs[k], x1 = xs[k + 1];
        if (x0 >= x1) continue;

        int nz = 0;
        for (int i = 0; i < count; i++)
        {
            const int minX = rects[4 * i + 0];
            const int maxX = rects[4 * i + 2];
            if (minX > maxX) continue;
            if (minX > x0 || maxX < x1 - 1) continue; /* must span this slab */
            zspan[nz++] = rects[4 * i + 1];
            zspan[nz++] = rects[4 * i + 3];
        }
        if (nz == 0) continue;

        /* Sort intervals by minZ (insertion sort; nz is small and nearly
         * ordered in practice), then sweep for the covered length. */
        for (int i = 0; i + 1 < nz; i += 2)
            for (int j = i + 2; j + 1 < nz; j += 2)
                if (zspan[j] < zspan[i])
                {
                    const int tm = zspan[i], tx = zspan[i + 1];
                    zspan[i] = zspan[j];
                    zspan[i + 1] = zspan[j + 1];
                    zspan[j] = tm;
                    zspan[j + 1] = tx;
                }

        long long covered = 0;
        int curMin = 0, curMax = 0, started = 0;
        for (int i = 0; i + 1 < nz; i += 2)
        {
            const int a = zspan[i], b = zspan[i + 1];
            if (a > b) continue;
            if (!started)
            {
                curMin = a;
                curMax = b;
                started = 1;
            }
            else if (a <= curMax + 1)
            {
                if (b > curMax) curMax = b;
            }
            else
            {
                covered += (long long)curMax - curMin + 1;
                curMin = a;
                curMax = b;
            }
        }
        if (started) covered += (long long)curMax - curMin + 1;

        total += covered * (long long)(x1 - x0);
    }
    return total;
}

/* ------------------------------------------------------------------------- */
/* Piece export                                                              */
/* ------------------------------------------------------------------------- */

static void fillPiece(StrongholdSimPiece *dst, const Piece *src, int order, int hasLoot)
{
    memset(dst, 0, sizeof(*dst));
    dst->type = src->type;
    dst->code = ssim_pieceTypeCode(src->type);
    dst->rot = src->rot;
    dst->depth = src->depth;
    dst->order = order;

    /* bb0/bb1 are world-space after orientBox(), and inclusive. */
    dst->minX = src->bb0.x;
    dst->maxX = src->bb1.x;
    dst->minZ = src->bb0.z;
    dst->maxZ = src->bb1.z;
    dst->nominalMinY = src->bb0.y;
    dst->nominalMaxY = src->bb1.y;
    dst->height = src->bb1.y - src->bb0.y + 1;

    /* getStrongholdPieces() never writes chestCount/chestPoses/lootTables, so
     * reading them on a pre-1.13 layout would expose uninitialised memory. */
    if (!hasLoot) return;

    int chests = src->chestCount;
    if (chests < 0) chests = 0;
    if (chests > SSIM_MAX_CHESTS) chests = SSIM_MAX_CHESTS;
    dst->chestCount = chests;
    for (int c = 0; c < chests; c++)
    {
        dst->chestX[c] = src->chestPoses[c].x;
        dst->chestZ[c] = src->chestPoses[c].z;
        dst->lootTable[c] = src->lootTables[c];
    }
}

/* Aggregate the piece list into the footprint / per-type fields. */
static void computeFootprint(StrongholdSim *portal, const StrongholdSimPiece *pieces, int count)
{
    if (!portal || !pieces || count <= 0) return;
    if (count > SSIM_PIECE_CAPACITY) count = SSIM_PIECE_CAPACITY;

    /* Stack scratch: 4 * 400 ints each, ~19 KiB total at the maximum size. */
    int rects[4 * SSIM_PIECE_CAPACITY];
    int scratch[4 * SSIM_PIECE_CAPACITY];
    int typeRects[4 * SSIM_PIECE_CAPACITY];

    for (int i = 0; i < count; i++)
    {
        rects[4 * i + 0] = pieces[i].minX;
        rects[4 * i + 1] = pieces[i].minZ;
        rects[4 * i + 2] = pieces[i].maxX;
        rects[4 * i + 3] = pieces[i].maxZ;
    }

    int minX = pieces[0].minX, maxX = pieces[0].maxX;
    int minZ = pieces[0].minZ, maxZ = pieces[0].maxZ;
    for (int i = 1; i < count; i++)
    {
        if (pieces[i].minX < minX) minX = pieces[i].minX;
        if (pieces[i].maxX > maxX) maxX = pieces[i].maxX;
        if (pieces[i].minZ < minZ) minZ = pieces[i].minZ;
        if (pieces[i].maxZ > maxZ) maxZ = pieces[i].maxZ;
    }
    portal->footprintMinX = minX;
    portal->footprintMaxX = maxX;
    portal->footprintMinZ = minZ;
    portal->footprintMaxZ = maxZ;
    portal->footprintWidth = maxX - minX + 1;
    portal->footprintDepth = maxZ - minZ + 1;
    portal->footprintArea = (int)ssim_unionArea(rects, count, scratch);
    portal->footprintChunkMinX = minX >> 4;
    portal->footprintChunkMaxX = maxX >> 4;
    portal->footprintChunkMinZ = minZ >> 4;
    portal->footprintChunkMaxZ = maxZ >> 4;

    for (int t = 0; t < SSIM_TYPE_COUNT; t++)
    {
        portal->typeCounts[t] = 0;
        portal->typeExtent[t].count = 0;
        portal->typeExtent[t].minX = portal->typeExtent[t].minZ = 0;
        portal->typeExtent[t].maxX = portal->typeExtent[t].maxZ = 0;
        portal->typeExtent[t].area = 0;
    }

    for (int t = 0; t < SSIM_TYPE_COUNT; t++)
    {
        int n = 0;
        for (int i = 0; i < count; i++)
        {
            if (pieces[i].type != t) continue;
            typeRects[4 * n + 0] = pieces[i].minX;
            typeRects[4 * n + 1] = pieces[i].minZ;
            typeRects[4 * n + 2] = pieces[i].maxX;
            typeRects[4 * n + 3] = pieces[i].maxZ;
            n++;
        }
        portal->typeCounts[t] = n;
        if (n == 0) continue;

        int tMinX = typeRects[0], tMaxX = typeRects[2];
        int tMinZ = typeRects[1], tMaxZ = typeRects[3];
        for (int i = 1; i < n; i++)
        {
            if (typeRects[4 * i + 0] < tMinX) tMinX = typeRects[4 * i + 0];
            if (typeRects[4 * i + 2] > tMaxX) tMaxX = typeRects[4 * i + 2];
            if (typeRects[4 * i + 1] < tMinZ) tMinZ = typeRects[4 * i + 1];
            if (typeRects[4 * i + 3] > tMaxZ) tMaxZ = typeRects[4 * i + 3];
        }
        portal->typeExtent[t].count = n;
        portal->typeExtent[t].minX = tMinX;
        portal->typeExtent[t].maxX = tMaxX;
        portal->typeExtent[t].minZ = tMinZ;
        portal->typeExtent[t].maxZ = tMaxZ;
        portal->typeExtent[t].area = (int)ssim_unionArea(typeRects, n, scratch);
    }
}

int ssim_footprintMask(const StrongholdSimPiece *pieces, int count, const StrongholdSim *portal,
                       uint8_t *bits, long long maxBlocks, int *outWidth, int *outHeight)
{
    if (!pieces || !portal || !bits || count <= 0) return 0;
    if (!portal->ok) return 0;

    const int width = portal->footprintWidth;
    const int height = portal->footprintDepth;
    if (width <= 0 || height <= 0) return 0;
    const long long cells = (long long)width * (long long)height;
    if (cells > maxBlocks) return 0;

    memset(bits, 0, (size_t)cells);
    const int originX = portal->footprintMinX;
    const int originZ = portal->footprintMinZ;

    for (int i = 0; i < count; i++)
    {
        const StrongholdSimPiece *pc = &pieces[i];
        int x0 = pc->minX - originX, x1 = pc->maxX - originX;
        int z0 = pc->minZ - originZ, z1 = pc->maxZ - originZ;
        if (x0 < 0) x0 = 0;
        if (z0 < 0) z0 = 0;
        if (x1 >= width) x1 = width - 1;
        if (z1 >= height) z1 = height - 1;
        if (x0 > x1 || z0 > z1) continue;
        for (int z = z0; z <= z1; z++)
        {
            uint8_t *row = bits + (size_t)z * (size_t)width;
            for (int x = x0; x <= x1; x++) row[x] = 1;
        }
    }

    if (outWidth) *outWidth = width;
    if (outHeight) *outHeight = height;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Main entry points                                                         */
/* ------------------------------------------------------------------------- */

static void localToWorld(const Piece *p, int lx, int lz, int *wx, int *wz)
{
    int x = lx, z = lz;
    rotPos(p->bb0, p->bb1, &x, &z, p->rot);
    *wx = x;
    *wz = z;
}

int ssim_compute(StrongholdSim *out, StrongholdSimPiece *pieces, int maxPieces,
                 int mc, uint64_t seed, int chunkX, int chunkZ)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    out->mc = mc;
    out->seed = seed;
    out->chunkX = chunkX;
    out->chunkZ = chunkZ;
    out->eyes = SSIM_EYES_UNKNOWN;
    out->portalRoomIndex = -1;
    out->startX = (chunkX << 4) + 4;
    out->startZ = (chunkZ << 4) + 4;

    if (!ssim_versionSupported(mc)) return 0;

    if (maxPieces < 0) maxPieces = 0;
    if (maxPieces > SSIM_PIECE_CAPACITY) maxPieces = SSIM_PIECE_CAPACITY;

    const int withLoot = ssim_lootSupported(mc);
    StructureSaltConfig ssconf;
    memset(&ssconf, 0, sizeof(ssconf));
    if (withLoot && !getStructureSaltConfig(Stronghold, mc, 0, &ssconf)) return 0;

    /* Always generate with the full cap: the piece layout is sensitive to the
     * buffer size, so a smaller caller request must only limit the copy-out. */
    Piece raw[SSIM_PIECE_CAPACITY];
    const int count = withLoot
        ? getStrongholdLoot(raw, SSIM_PIECE_CAPACITY, ssconf, mc, seed, chunkX, chunkZ)
        : getStrongholdPieces(raw, SSIM_PIECE_CAPACITY, mc, seed, chunkX, chunkZ);
    if (count <= 0) return 0;

    out->pieces = count;
    if (count >= SSIM_PIECE_CAPACITY) out->truncated = 1;

    /* Build the complete view once: the footprint needs every piece regardless
     * of how many the caller wants exported. */
    StrongholdSimPiece all[SSIM_PIECE_CAPACITY];
    for (int i = 0; i < count; i++) fillPiece(&all[i], &raw[i], i, withLoot);
    computeFootprint(out, all, count);

    if (pieces && maxPieces > 0)
    {
        const int exportCount = count < maxPieces ? count : maxPieces;
        for (int i = 0; i < exportCount; i++) pieces[i] = all[i];
    }

    const Piece *p = NULL;
    int portalIndex = -1;
    for (int i = 0; i < count; i++)
        if (raw[i].type == SH_PORTAL_ROOM)
        {
            p = &raw[i];
            portalIndex = i;
            break;
        }
    /* Never derive coordinates from a truncated layout: the room may be among
     * the pieces that were dropped. */
    if (!p) return count;

    out->ok = 1;
    out->truncated = 0;
    out->portalRoomIndex = portalIndex;

    out->roomMinX = p->bb0.x;
    out->roomMinZ = p->bb0.z;
    out->roomMaxX = p->bb1.x;
    out->roomMaxZ = p->bb1.z;
    out->roomNominalMinY = p->bb0.y;
    out->roomNominalMaxY = p->bb1.y;

    {
        int ax, az, bx, bz, cx, cz;
        localToWorld(p, PORTAL_CORNER_A.x, PORTAL_CORNER_A.z, &ax, &az);
        localToWorld(p, PORTAL_CORNER_B.x, PORTAL_CORNER_B.z, &bx, &bz);
        localToWorld(p, PORTAL_CENTER.x, PORTAL_CENTER.z, &cx, &cz);
        out->frameMinX = ax < bx ? ax : bx;
        out->frameMaxX = ax < bx ? bx : ax;
        out->frameMinZ = az < bz ? az : bz;
        out->frameMaxZ = az < bz ? bz : az;
        out->portalX = cx;
        out->portalZ = cz;
    }

    for (int j = 0; j < SSIM_EYE_COUNT; j++)
        localToWorld(p, EYE_LOCAL[j].x, EYE_LOCAL[j].z,
                     &out->eyeFrameX[j], &out->eyeFrameZ[j]);

    if (withLoot)
    {
        out->eyeMask = (unsigned)p->additionalData & 0xFFFu;
        int filled = 0;
        for (int j = 0; j < SSIM_EYE_COUNT; j++)
            if (out->eyeMask & (1u << j)) filled++;
        out->eyes = filled;
    }

    return count;
}

int ssim_locateAll(StrongholdSim *out, int maxCount, int mc, uint64_t seed)
{
    if (!out || maxCount <= 0) return 0;
    if (!ssim_versionSupported(mc)) return 0;
    if (maxCount > SSIM_MAX_STRONGHOLDS) maxCount = SSIM_MAX_STRONGHOLDS;

    if (!ssim_prepare(mc, seed)) return 0;

    StrongholdIter iter;
    initFirstStronghold(&iter, mc, seed);

    int written = 0;
    for (int i = 0; i < maxCount; i++)
    {
        if (nextStronghold(&iter, &g_gen) <= 0) break;
        StrongholdSim *slot = &out[written];
        ssim_compute(slot, NULL, 0, mc, seed, iter.pos.x >> 4, iter.pos.z >> 4);
        /* Keep the iterator's authoritative start point. */
        slot->startX = iter.pos.x;
        slot->startZ = iter.pos.z;
        written++;
    }
    return written;
}

int ssim_locateFirst(StrongholdSim *out, int mc, uint64_t seed)
{
    if (!out) return 0;
    if (!ssim_versionSupported(mc)) return 0;
    if (!ssim_prepare(mc, seed)) return 0;

    StrongholdIter iter;
    initFirstStronghold(&iter, mc, seed);
    if (nextStronghold(&iter, &g_gen) <= 0) return 0;

    ssim_compute(out, NULL, 0, mc, seed, iter.pos.x >> 4, iter.pos.z >> 4);
    out->startX = iter.pos.x;
    out->startZ = iter.pos.z;
    return out->ok;
}
