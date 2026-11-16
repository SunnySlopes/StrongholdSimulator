/*
 * stronghold-sim CLI — command-line front end for the stronghold simulator.
 *
 * Examples:
 *   stronghold-sim --seed 123 --version 1.21.6 --limit 6
 *   stronghold-sim --seed 123 --version 26.3 --min-eyes 3
 *   stronghold-sim --seed 123 --version 1.21.6 --index 0 --pieces
 *   stronghold-sim --seed 123 --version 1.21.6 --index 0 --footprint --mask
 *   stronghold-sim --seed 123 --version 1.21.6 --json
 *   stronghold-sim --seed 123 --version 1.21.6 --csv --footprint
 *   stronghold-sim --versions            (list every supported version)
 *   stronghold-sim --seed 123 --all-versions   (same seed, every version)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stronghold_sim.h"

static void usage(FILE *fp)
{
    fputs(
        "stronghold-sim — stronghold End portal and layout simulator\n"
        "\n"
        "USAGE: stronghold-sim [options]\n"
        "\n"
        "  --seed <value>     world seed (decimal, hex 0x.., or any text)   [0]\n"
        "  --version <text>   Minecraft version, e.g. 1.21.6, 26.3         [newest]\n"
        "  --limit <n>        stop after n strongholds                      [128]\n"
        "  --index <n>        show only stronghold #n (0-based)\n"
        "  --eyes <n>         only strongholds with exactly n eyes\n"
        "  --min-eyes <n>     only strongholds with at least n eyes\n"
        "  --pieces           also list every generated piece\n"
        "  --type <name>      with --pieces: only that piece type\n"
        "  --footprint        also show whole-stronghold planar coverage\n"
        "  --mask             with --footprint: render coverage as ASCII art\n"
        "  --csv              CSV output\n"
        "  --json             JSON output\n"
        "  --quiet            omit the summary block\n"
        "  --check            verify invariants, exit 1 on failure\n"
        "  --versions         list supported versions and exit\n"
        "  --all-versions     run the same seed across every supported version\n"
        "  -h, --help         show this help\n"
        "\n"
        "Coordinates are block X/Z of the 3x3 end_portal area's centre.\n"
        "No world Y is printed: cubiomes does not model vertical piece\n"
        "placement, so every Y here is nominal (anchored at y=64).\n",
        fp);
}

static int parseSeed(const char *text, uint64_t *out)
{
    if (!text || !*text) return 0;
    char *end = NULL;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        const unsigned long long v = strtoull(text + 2, &end, 16);
        if (end && *end == '\0') { *out = (uint64_t)v; return 1; }
        return 0;
    }
    const long long v = strtoll(text, &end, 10);
    if (end && *end == '\0') { *out = (uint64_t)v; return 1; }

    /* Java String.hashCode, matching the game's text-seed behaviour. */
    int32_t h = 0;
    for (const unsigned char *s = (const unsigned char *)text; *s; s++)
        h = 31 * h + (int32_t)*s;
    *out = (uint64_t)(int64_t)h;
    return 1;
}

static int parseInt(const char *text, int *out)
{
    char *end = NULL;
    const long v = strtol(text, &end, 10);
    if (!end || *end != '\0') return 0;
    *out = (int)v;
    return 1;
}

static int parsePieceType(const char *text)
{
    if (!strcmp(text, "straight")) return SSIM_STRAIGHT;
    if (!strcmp(text, "prison")) return SSIM_PRISON_HALL;
    if (!strcmp(text, "left")) return SSIM_LEFT_TURN;
    if (!strcmp(text, "right")) return SSIM_RIGHT_TURN;
    if (!strcmp(text, "crossing")) return SSIM_ROOM_CROSSING;
    if (!strcmp(text, "straight_stairs")) return SSIM_STRAIGHT_STAIRS_DOWN;
    if (!strcmp(text, "stairs")) return SSIM_STAIRS_DOWN;
    if (!strcmp(text, "five")) return SSIM_FIVE_CROSSING;
    if (!strcmp(text, "chest")) return SSIM_CHEST_CORRIDOR;
    if (!strcmp(text, "library")) return SSIM_LIBRARY;
    if (!strcmp(text, "portal")) return SSIM_PORTAL_ROOM;
    if (!strcmp(text, "filler")) return SSIM_FILLER_CORRIDOR;
    return -1;
}

/* ------------------------------------------------------------------------- */
/* Output helpers                                                            */
/* ------------------------------------------------------------------------- */

static void jsonString(const char *text)
{
    putchar('"');
    for (const char *s = text; *s; s++)
    {
        if (*s == '"' || *s == '\\') printf("\\%c", *s);
        else if ((unsigned char)*s < 0x20) printf("\\u%04x", (unsigned char)*s);
        else putchar(*s);
    }
    putchar('"');
}

static void printHeader(void)
{
    printf("%4s  %8s %8s  %9s %9s  %5s  %-22s  %s\n",
           "#", "start X", "start Z", "portal X", "portal Z", "eyes", "3x3 frame", "status");
    printf("%4s  %8s %8s  %9s %9s  %5s  %-22s  %s\n",
           "----", "--------", "--------", "---------", "---------", "-----",
           "----------------------", "------");
}

static void printRow(int index, const StrongholdSim *p)
{
    char eyes[8], frame[48], px[16], pz[16];
    if (p->eyes == SSIM_EYES_UNKNOWN) snprintf(eyes, sizeof(eyes), "?");
    else snprintf(eyes, sizeof(eyes), "%d/%d", p->eyes, SSIM_EYE_COUNT);

    if (p->ok)
    {
        snprintf(frame, sizeof(frame), "%d..%d, %d..%d",
                 p->frameMinX, p->frameMaxX, p->frameMinZ, p->frameMaxZ);
        snprintf(px, sizeof(px), "%d", p->portalX);
        snprintf(pz, sizeof(pz), "%d", p->portalZ);
    }
    else
    {
        snprintf(frame, sizeof(frame), "-");
        snprintf(px, sizeof(px), "-");
        snprintf(pz, sizeof(pz), "-");
    }

    printf("%4d  %8d %8d  %9s %9s  %5s  %-22s  %s\n",
           index, p->startX, p->startZ, px, pz, eyes, frame,
           p->ok ? "ok" : (p->truncated ? "truncated" : "no portal room"));
}

static void printFootprint(const StrongholdSim *p)
{
    printf("      footprint : X %d..%d  Z %d..%d   (%d x %d blocks)\n",
           p->footprintMinX, p->footprintMaxX, p->footprintMinZ, p->footprintMaxZ,
           p->footprintWidth, p->footprintDepth);
    printf("      chunks    : X %d..%d  Z %d..%d   (%d chunks)\n",
           p->footprintChunkMinX, p->footprintChunkMaxX,
           p->footprintChunkMinZ, p->footprintChunkMaxZ,
           (p->footprintChunkMaxX - p->footprintChunkMinX + 1) *
               (p->footprintChunkMaxZ - p->footprintChunkMinZ + 1));
    const long long box = (long long)p->footprintWidth * (long long)p->footprintDepth;
    printf("      covered   : %d blocks of %lld in the box (%.1f%%)\n",
           p->footprintArea, box, box > 0 ? 100.0 * p->footprintArea / (double)box : 0.0);
    printf("      by type   :\n");
    for (int t = 0; t < SSIM_TYPE_COUNT; t++)
    {
        if (p->typeCounts[t] <= 0) continue;
        printf("        %-5s %-20s x%-3d  X %d..%d  Z %d..%d  (%d blocks)\n",
               ssim_pieceTypeCode(t), ssim_pieceTypeName(t), p->typeCounts[t],
               p->typeExtent[t].minX, p->typeExtent[t].maxX,
               p->typeExtent[t].minZ, p->typeExtent[t].maxZ,
               p->typeExtent[t].area);
    }
}

static void printMask(const StrongholdSim *p)
{
    StrongholdSimPiece pieces[SSIM_PIECE_CAPACITY];
    StrongholdSim scratch;
    const int n = ssim_compute(&scratch, pieces, SSIM_PIECE_CAPACITY,
                               p->mc, p->seed, p->chunkX, p->chunkZ);
    if (n <= 0) return;

    /* 132x108 blocks is typical; 400x400 covers anything cubiomes produces. */
    static uint8_t bits[400 * 400];
    const long long cells = (long long)p->footprintWidth * (long long)p->footprintDepth;
    if (cells > (long long)sizeof(bits))
    {
        printf("      (mask too large: %lld blocks)\n", cells);
        return;
    }
    int w = 0, h = 0;
    if (!ssim_footprintMask(pieces, n, p, bits, (long long)sizeof(bits), &w, &h))
    {
        printf("      (mask unavailable)\n");
        return;
    }

    int stride = (w > 100 || h > 60) ? ((w + 99) / 100) : 1;
    if ((h + stride - 1) / stride > 60) stride = (h + 59) / 60;

    printf("      mask      : 1 block = %dx%d, '#' covered, '.' empty\n", stride, stride);
    printf("      origin    : (%d, %d), size %d x %d\n", p->footprintMinX, p->footprintMinZ, w, h);
    for (int z = 0; z < h; z += stride)
    {
        printf("      ");
        for (int x = 0; x < w; x += stride)
        {
            int hit = 0;
            for (int dz = 0; dz < stride && !hit; dz++)
                for (int dx = 0; dx < stride && !hit; dx++)
                {
                    const int zz = z + dz, xx = x + dx;
                    if (zz < h && xx < w && bits[(size_t)zz * (size_t)w + (size_t)xx]) hit = 1;
                }
            putchar(hit ? '#' : '.');
        }
        putchar('\n');
    }
}

static void printPieceTable(const StrongholdSim *p, int typeFilter)
{
    StrongholdSimPiece pieces[SSIM_PIECE_CAPACITY];
    StrongholdSim scratch;
    const int n = ssim_compute(&scratch, pieces, SSIM_PIECE_CAPACITY,
                               p->mc, p->seed, p->chunkX, p->chunkZ);
    printf("      %3s %-5s %3s %5s  %11s %11s  %11s %s\n",
           "ord", "code", "rot", "depth", "X range", "Z range", "nominal Y", "chests");
    for (int k = 0; k < n; k++)
    {
        const StrongholdSimPiece *pc = &pieces[k];
        if (typeFilter >= 0 && pc->type != typeFilter) continue;
        char xr[32], zr[32], yr[32], chests[72];
        snprintf(xr, sizeof(xr), "%d..%d", pc->minX, pc->maxX);
        snprintf(zr, sizeof(zr), "%d..%d", pc->minZ, pc->maxZ);
        snprintf(yr, sizeof(yr), "%d..%d", pc->nominalMinY, pc->nominalMaxY);
        if (pc->chestCount > 0)
            snprintf(chests, sizeof(chests), "%d @ (%d,%d)%s", pc->chestCount,
                     pc->chestX[0], pc->chestZ[0], pc->chestCount > 1 ? " ..." : "");
        else snprintf(chests, sizeof(chests), "%s", "-");
        printf("      %3d %-5s %3d %5d  %11s %11s  %11s %s\n",
               pc->order, pc->code, pc->rot, pc->depth, xr, zr, yr, chests);
    }
    printf("      (%d pieces)\n", n);
}

/* ------------------------------------------------------------------------- */
/* One simulation run                                                        */
/* ------------------------------------------------------------------------- */

typedef struct
{
    uint64_t seed;
    int mc;
    int indexOnly, limit, minEyes, exactEyes;
    int csv, json, quiet, check, wantPieces, wantFootprint, wantMask;
    int typeFilter;
} Options;

/** Returns the process exit code. */
static int runOnce(const Options *opt)
{
    const int mc = opt->mc;
    const uint64_t seed = opt->seed;

    if (!ssim_versionSupported(mc))
    {
        fprintf(stderr, "error: version %s is outside the supported range (%s .. %s)\n",
                ssim_versionName(mc), ssim_versionName(ssim_minVersion()),
                ssim_versionName(ssim_maxVersion()));
        return 2;
    }

    int limit = opt->limit;
    if (limit <= 0) limit = 1;
    if (limit > SSIM_MAX_STRONGHOLDS) limit = SSIM_MAX_STRONGHOLDS;

    static StrongholdSim list[SSIM_MAX_STRONGHOLDS];
    memset(list, 0, sizeof(list));
    const int found = ssim_locateAll(list, limit, mc, seed);

    int okCount = 0, truncCount = 0, failCount = 0;
    int eyeTotal = 0, eyeKnown = 0;
    for (int i = 0; i < found; i++)
    {
        const StrongholdSim *p = &list[i];
        if (p->ok) okCount++;
        else if (p->truncated) truncCount++;
        else failCount++;
        if (p->eyes != SSIM_EYES_UNKNOWN) { eyeTotal += p->eyes; eyeKnown++; }
    }

    /* Row filter shared by every output mode. */
    #define SELECTED(i, p)                                                        \
        ((opt->indexOnly < 0 || (i) == opt->indexOnly) && (p)->ok &&              \
         (opt->exactEyes < 0 || (p)->eyes == opt->exactEyes) &&                   \
         (opt->minEyes < 0 ||                                                     \
          ((p)->eyes != SSIM_EYES_UNKNOWN && (p)->eyes >= opt->minEyes)))

    int shown = 0;

    if (opt->json)
    {
        printf("{\n");
        printf("  \"seed\": \"%lld\",\n", (long long)seed);
        printf("  \"mc\": %d,\n", mc);
        printf("  \"version\": "); jsonString(ssim_versionName(mc)); printf(",\n");
        printf("  \"lootSupported\": %s,\n", ssim_lootSupported(mc) ? "true" : "false");
        printf("  \"nominalStartY\": %d,\n", SSIM_NOMINAL_START_Y);
        printf("  \"count\": %d,\n", found);
        printf("  \"strongholds\": [");
        int first = 1;
        for (int i = 0; i < found; i++)
        {
            const StrongholdSim *p = &list[i];
            if (!SELECTED(i, p)) continue;
            printf("%s\n    {\n", first ? "" : ",");
            printf("      \"startX\": %d, \"startZ\": %d,\n", p->startX, p->startZ);
            printf("      \"ok\": true, \"truncated\": false, \"pieces\": %d,\n", p->pieces);
            printf("      \"portalX\": %d, \"portalZ\": %d,\n", p->portalX, p->portalZ);
            printf("      \"frame\": { \"minX\": %d, \"minZ\": %d, \"maxX\": %d, \"maxZ\": %d },\n",
                   p->frameMinX, p->frameMinZ, p->frameMaxX, p->frameMaxZ);
            if (p->eyes == SSIM_EYES_UNKNOWN) printf("      \"eyes\": null,\n");
            else printf("      \"eyes\": %d,\n", p->eyes);
            if (opt->wantFootprint)
            {
                printf("      \"footprint\": {\n");
                printf("        \"minX\": %d, \"maxX\": %d, \"minZ\": %d, \"maxZ\": %d,\n",
                       p->footprintMinX, p->footprintMaxX, p->footprintMinZ, p->footprintMaxZ);
                printf("        \"width\": %d, \"depth\": %d, \"area\": %d,\n",
                       p->footprintWidth, p->footprintDepth, p->footprintArea);
                printf("        \"types\": [");
                int firstType = 1;
                for (int t = 0; t < SSIM_TYPE_COUNT; t++)
                {
                    if (p->typeCounts[t] <= 0) continue;
                    printf("%s\n          { \"code\": ", firstType ? "" : ",");
                    jsonString(ssim_pieceTypeCode(t));
                    printf(", \"count\": %d, \"area\": %d, \"minX\": %d, \"maxX\": %d,"
                           " \"minZ\": %d, \"maxZ\": %d }",
                           p->typeCounts[t], p->typeExtent[t].area,
                           p->typeExtent[t].minX, p->typeExtent[t].maxX,
                           p->typeExtent[t].minZ, p->typeExtent[t].maxZ);
                    firstType = 0;
                }
                printf("%s]\n      }\n", firstType ? "" : "\n        ");
            }
            else printf("      \"footprint\": null\n");
            printf("    }");
            first = 0;
            shown++;
        }
        printf("%s],\n", first ? "" : "\n  ");
        printf("  \"shown\": %d\n}\n", shown);
        return okCount > 0 ? 0 : 1;
    }

    if (opt->csv)
    {
        if (opt->wantPieces)
        {
            printf("index,order,type,code,rot,depth,minX,maxX,minZ,maxZ,"
                   "nominalMinY,nominalMaxY,height,chestCount,chest1X,chest1Z,"
                   "chest1Loot,chest2X,chest2Z,chest2Loot\n");
            for (int i = 0; i < found; i++)
            {
                const StrongholdSim *p = &list[i];
                if (!SELECTED(i, p)) continue;
                StrongholdSimPiece pieces[SSIM_PIECE_CAPACITY];
                StrongholdSim scratch;
                const int n = ssim_compute(&scratch, pieces, SSIM_PIECE_CAPACITY,
                                           mc, seed, p->chunkX, p->chunkZ);
                for (int k = 0; k < n; k++)
                {
                    const StrongholdSimPiece *pc = &pieces[k];
                    if (opt->typeFilter >= 0 && pc->type != opt->typeFilter) continue;
                    printf("%d,%d,%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                           i, pc->order, pc->type, pc->code, pc->rot, pc->depth,
                           pc->minX, pc->maxX, pc->minZ, pc->maxZ,
                           pc->nominalMinY, pc->nominalMaxY, pc->height, pc->chestCount);
                    for (int c = 0; c < 2; c++)
                    {
                        if (c < pc->chestCount)
                            printf(",%d,%d,%s", pc->chestX[c], pc->chestZ[c],
                                   pc->lootTable[c] ? pc->lootTable[c] : "");
                        else printf(",,,");
                    }
                    printf("\n");
                }
            }
            return okCount > 0 ? 0 : 1;
        }
        if (opt->wantFootprint)
        {
            printf("index,footprintMinX,footprintMaxX,footprintMinZ,footprintMaxZ,"
                   "footprintWidth,footprintDepth,footprintArea,type,typeCount,typeArea\n");
            for (int i = 0; i < found; i++)
            {
                const StrongholdSim *p = &list[i];
                if (!SELECTED(i, p)) continue;
                printf("%d,%d,%d,%d,%d,%d,%d,%d,TOTAL,-,%d\n", i,
                       p->footprintMinX, p->footprintMaxX, p->footprintMinZ, p->footprintMaxZ,
                       p->footprintWidth, p->footprintDepth, p->footprintArea, p->footprintArea);
                for (int t = 0; t < SSIM_TYPE_COUNT; t++)
                {
                    if (p->typeCounts[t] <= 0) continue;
                    printf("%d,%d,%d,%d,%d,%d,%d,%d,%s,%d,%d\n", i,
                           p->footprintMinX, p->footprintMaxX, p->footprintMinZ, p->footprintMaxZ,
                           p->footprintWidth, p->footprintDepth, p->footprintArea,
                           ssim_pieceTypeCode(t), p->typeCounts[t], p->typeExtent[t].area);
                }
            }
            return okCount > 0 ? 0 : 1;
        }
        printf("index,startX,startZ,portalX,portalZ,eyes,eyeMask,pieceCount,"
               "frameMinX,frameMinZ,frameMaxX,frameMaxZ,status\n");
        for (int i = 0; i < found; i++)
        {
            const StrongholdSim *p = &list[i];
            if (!SELECTED(i, p)) continue;
            printf("%d,%d,%d,%d,%d,%d,0x%03X,%d,%d,%d,%d,%d,ok\n", i,
                   p->startX, p->startZ, p->portalX, p->portalZ,
                   p->eyes == SSIM_EYES_UNKNOWN ? -1 : p->eyes, p->eyeMask, p->pieces,
                   p->frameMinX, p->frameMinZ, p->frameMaxX, p->frameMaxZ);
            shown++;
        }
        return okCount > 0 ? 0 : 1;
    }

    if (!opt->quiet)
    {
        printf("seed     : %lld\n", (long long)seed);
        printf("version  : %s\n", ssim_versionName(mc));
        printf("eyes     : %s\n",
               ssim_lootSupported(mc) ? "modelled" : "not modelled below 1.13");
        printf("\n");
    }

    printHeader();
    for (int i = 0; i < found; i++)
    {
        const StrongholdSim *p = &list[i];
        if (!SELECTED(i, p)) continue;
        printRow(i, p);
        shown++;
        if (opt->wantFootprint) printFootprint(p);
        if (opt->wantMask) printMask(p);
        if (opt->wantPieces) printPieceTable(p, opt->typeFilter);
    }

    if (!opt->quiet)
    {
        printf("\n");
        printf("strongholds : %d located (%d ok, %d truncated, %d without portal room)\n",
               found, okCount, truncCount, failCount);
        if (eyeKnown > 0)
            printf("eyes        : %d known, %d total (avg %.2f)\n",
                   eyeKnown, eyeTotal, (double)eyeTotal / eyeKnown);
        printf("shown       : %d\n", shown);
    }

    if (opt->check)
    {
        int failures = 0;
        for (int i = 0; i < found; i++)
        {
            const StrongholdSim *p = &list[i];
            if (!p->ok) continue;

            if (p->frameMaxX - p->frameMinX != 2 || p->frameMaxZ - p->frameMinZ != 2)
            {
                fprintf(stderr, "check: #%d frame is not 3x3\n", i);
                failures++;
            }
            if (p->portalX != (p->frameMinX + p->frameMaxX) / 2 ||
                p->portalZ != (p->frameMinZ + p->frameMaxZ) / 2)
            {
                fprintf(stderr, "check: #%d portal is not the frame centre\n", i);
                failures++;
            }
            if (p->eyes != SSIM_EYES_UNKNOWN &&
                (p->eyes < 0 || p->eyes > SSIM_EYE_COUNT))
            {
                fprintf(stderr, "check: #%d eye count out of range: %d\n", i, p->eyes);
                failures++;
            }
            if (p->portalX < p->roomMinX || p->portalX > p->roomMaxX ||
                p->portalZ < p->roomMinZ || p->portalZ > p->roomMaxZ)
            {
                fprintf(stderr, "check: #%d portal outside its room\n", i);
                failures++;
            }
            if (p->footprintArea <= 0 ||
                (long long)p->footprintArea > (long long)p->footprintWidth * p->footprintDepth)
            {
                fprintf(stderr, "check: #%d footprint area out of range\n", i);
                failures++;
            }
            if (p->portalX < p->footprintMinX || p->portalX > p->footprintMaxX ||
                p->portalZ < p->footprintMinZ || p->portalZ > p->footprintMaxZ)
            {
                fprintf(stderr, "check: #%d portal outside the footprint\n", i);
                failures++;
            }
            int sumTypes = 0;
            for (int t = 0; t < SSIM_TYPE_COUNT; t++) sumTypes += p->typeCounts[t];
            if (sumTypes != p->pieces)
            {
                fprintf(stderr, "check: #%d per-type counts sum to %d, expected %d\n",
                        i, sumTypes, p->pieces);
                failures++;
            }
            /* Piece-level: exactly one portal room, boxes not inverted. */
            StrongholdSimPiece pieces[SSIM_PIECE_CAPACITY];
            StrongholdSim scratch;
            const int n = ssim_compute(&scratch, pieces, SSIM_PIECE_CAPACITY,
                                       mc, seed, p->chunkX, p->chunkZ);
            int rooms = 0;
            for (int k = 0; k < n; k++)
            {
                if (pieces[k].type == SSIM_PORTAL_ROOM) rooms++;
                if (pieces[k].minX > pieces[k].maxX || pieces[k].minZ > pieces[k].maxZ ||
                    pieces[k].nominalMinY > pieces[k].nominalMaxY)
                {
                    fprintf(stderr, "check: #%d piece %d has an inverted box\n", i, k);
                    failures++;
                }
                for (int c = 0; c < pieces[k].chestCount; c++)
                    if (pieces[k].chestX[c] < pieces[k].minX || pieces[k].chestX[c] > pieces[k].maxX ||
                        pieces[k].chestZ[c] < pieces[k].minZ || pieces[k].chestZ[c] > pieces[k].maxZ)
                    {
                        fprintf(stderr, "check: #%d piece %d chest %d outside its box\n", i, k, c);
                        failures++;
                    }
            }
            if (rooms != 1)
            {
                fprintf(stderr, "check: #%d has %d portal rooms (expected 1)\n", i, rooms);
                failures++;
            }
        }
        if (failures)
        {
            fprintf(stderr, "check: %d failure(s)\n", failures);
            return 1;
        }
        fprintf(stderr, "check: all invariants hold (%d strongholds)\n", okCount);
    }

    return okCount > 0 ? 0 : 1;
    #undef SELECTED
}

int main(int argc, char **argv)
{
    Options opt;
    memset(&opt, 0, sizeof(opt));
    opt.seed = 0;
    opt.mc = ssim_maxVersion();
    opt.indexOnly = -1;
    opt.limit = SSIM_MAX_STRONGHOLDS;
    opt.minEyes = -1;
    opt.exactEyes = -1;
    opt.typeFilter = -1;

    int listVersions = 0, allVersions = 0;

    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];
        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) { usage(stdout); return 0; }
        else if (!strcmp(arg, "--seed") && i + 1 < argc)
        {
            if (!parseSeed(argv[++i], &opt.seed))
            {
                fprintf(stderr, "error: bad seed '%s'\n", argv[i]);
                return 2;
            }
        }
        else if (!strcmp(arg, "--version") && i + 1 < argc)
        {
            /* Match the version text against the supported table. */
            const char *text = argv[++i];
            int found = -1;
            for (int v = 0; v < ssim_versionCount(); v++)
            {
                const int id = ssim_versionAt(v);
                if (!strcmp(ssim_versionName(id), text)) { found = id; break; }
            }
            if (found < 0)
            {
                fprintf(stderr, "error: unknown version '%s' (try --versions)\n", text);
                return 2;
            }
            opt.mc = found;
        }
        else if (!strcmp(arg, "--limit") && i + 1 < argc)
        {
            if (!parseInt(argv[++i], &opt.limit)) { fprintf(stderr, "error: bad --limit\n"); return 2; }
        }
        else if (!strcmp(arg, "--index") && i + 1 < argc)
        {
            if (!parseInt(argv[++i], &opt.indexOnly)) { fprintf(stderr, "error: bad --index\n"); return 2; }
        }
        else if (!strcmp(arg, "--eyes") && i + 1 < argc)
        {
            if (!parseInt(argv[++i], &opt.exactEyes)) { fprintf(stderr, "error: bad --eyes\n"); return 2; }
        }
        else if (!strcmp(arg, "--min-eyes") && i + 1 < argc)
        {
            if (!parseInt(argv[++i], &opt.minEyes)) { fprintf(stderr, "error: bad --min-eyes\n"); return 2; }
        }
        else if (!strcmp(arg, "--pieces")) opt.wantPieces = 1;
        else if (!strcmp(arg, "--footprint")) opt.wantFootprint = 1;
        else if (!strcmp(arg, "--mask")) { opt.wantMask = 1; opt.wantFootprint = 1; }
        else if (!strcmp(arg, "--type") && i + 1 < argc)
        {
            opt.typeFilter = parsePieceType(argv[++i]);
            if (opt.typeFilter < 0)
            {
                fprintf(stderr, "error: unknown piece type '%s'\n", argv[i]);
                return 2;
            }
            opt.wantPieces = 1;
        }
        else if (!strcmp(arg, "--csv")) opt.csv = 1;
        else if (!strcmp(arg, "--json")) opt.json = 1;
        else if (!strcmp(arg, "--quiet")) opt.quiet = 1;
        else if (!strcmp(arg, "--check")) opt.check = 1;
        else if (!strcmp(arg, "--versions")) listVersions = 1;
        else if (!strcmp(arg, "--all-versions")) allVersions = 1;
        else
        {
            fprintf(stderr, "error: unknown argument '%s' (try --help)\n", arg);
            return 2;
        }
    }

    if (listVersions)
    {
        printf("supported versions (%d):\n", ssim_versionCount());
        for (int v = 0; v < ssim_versionCount(); v++)
        {
            const int id = ssim_versionAt(v);
            printf("  %-10s loot=%s\n", ssim_versionName(id),
                   ssim_lootSupported(id) ? "yes" : "no ");
        }
        return 0;
    }

    if (allVersions)
    {
        /* Same seed, every version: useful for spotting version routing bugs. */
        printf("%-10s %10s %10s %10s %6s %s\n",
               "version", "start X", "portal X", "portal Z", "eyes", "pieces");
        for (int v = 0; v < ssim_versionCount(); v++)
        {
            const int id = ssim_versionAt(v);
            StrongholdSim p;
            if (!ssim_locateFirst(&p, id, opt.seed))
            {
                printf("%-10s %10s\n", ssim_versionName(id), "(none)");
                continue;
            }
            char eyes[8];
            if (p.eyes == SSIM_EYES_UNKNOWN) snprintf(eyes, sizeof(eyes), "?");
            else snprintf(eyes, sizeof(eyes), "%d", p.eyes);
            printf("%-10s %10d %10d %10d %6s %d\n",
                   ssim_versionName(id), p.startX, p.portalX, p.portalZ, eyes, p.pieces);
        }
        return 0;
    }

    const int rc = runOnce(&opt);
    ssim_release();
    return rc;
}
