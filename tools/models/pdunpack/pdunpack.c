// pdunpack: extract files from Perfect Dark XBLA's data files, without running the game.
//
//   pdunpack pack <PackedSegFile> <ids> <out dir>   -> <out dir>\file_XXXX.bin
//   pdunpack tex  <Textures.raw>  <ids> <out dir>   -> <out dir>\tex_XXXX.bin
//
// <ids> = "all", or hex ids and ranges separated by commas, e.g. "38D,393" or "329-32F".
// Texture output is the raw texel data as the game puts it in GPU memory (Xenos layout:
// tiled 8888 or DXT, 16-bit byte-swapped). tools/models/textures.py turns it into PNG.
//
// Both files use 4J's containers (see docs/research/gun-models.md):
//   PackedSegFile: u32 count, count x {u32 offset, size, compressed size (0 = stored), -1}.
//   Textures.raw:  u32 count, count x 52-byte record {u32 offset, ..., +20 size, +24 compressed size},
//                  count x 52-byte D3D header, then the data (offsets are from the data start).
// Compressed data is XMemCompress "native" LZX: a series of chunks, each either
//   FF uu uu cc cc <cc bytes>   (uu = inflated size of this chunk)
//   cc cc <cc bytes>            (inflated size 0x8000)
// The chunk payloads, joined, are one LZX stream (128 KB window), which libmspack decodes.
//
// Build: build.bat (clang from F:\Workspace\LLVM, libmspack from rexglue-sdk/thirdparty).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mspack.h"
#include "lzx.h"

// --- in-memory "files" for libmspack (it normally reads and writes real files) ---
typedef struct {
  unsigned char* buf;
  size_t size, pos;
} MemFile;

static int mem_read(struct mspack_file* f, void* b, int n) {
  MemFile* m = (MemFile*)f;
  size_t left = m->size - m->pos;
  if ((size_t)n > left) n = (int)left;
  memcpy(b, m->buf + m->pos, n);
  m->pos += n;
  return n;
}
static int mem_write(struct mspack_file* f, void* b, int n) {
  MemFile* m = (MemFile*)f;
  size_t left = m->size - m->pos;
  if ((size_t)n > left) n = (int)left;
  memcpy(m->buf + m->pos, b, n);
  m->pos += n;
  return n;
}
static struct mspack_file* mem_open(struct mspack_system* s, const char* n, int m) { return NULL; }
static void mem_close(struct mspack_file* f) {}
static int mem_seek(struct mspack_file* f, off_t o, int mode) { return -1; }
static off_t mem_tell(struct mspack_file* f) { return (off_t)((MemFile*)f)->pos; }
static void mem_message(struct mspack_file* f, const char* fmt, ...) {}
static void* mem_alloc(struct mspack_system* s, size_t n) { return calloc(n, 1); }
static void mem_free(void* p) { free(p); }
static void mem_copy(void* src, void* dst, size_t n) { memcpy(dst, src, n); }

static struct mspack_system g_sys = {mem_open, mem_close, mem_read,    mem_write, mem_seek,
                                     mem_tell, mem_message, mem_alloc, mem_free, mem_copy, NULL};

static unsigned be32(const unsigned char* p) {
  return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

// Inflate one XMem-compressed blob (src, csize) into out (size bytes). Returns 0 on success.
static int xmem_inflate(const unsigned char* src, unsigned csize, unsigned char* out, unsigned size) {
  // Join the chunk payloads, dropping the chunk headers.
  unsigned char* joined = malloc(csize);
  size_t joined_len = 0, p = 0, inflated = 0;
  while (p < csize && inflated < size) {
    unsigned chunk_out = 0x8000, chunk_in;
    if (src[p] == 0xFF) {
      chunk_out = (src[p + 1] << 8) | src[p + 2];
      chunk_in = (src[p + 3] << 8) | src[p + 4];
      p += 5;
    } else {
      chunk_in = (src[p] << 8) | src[p + 1];
      p += 2;
    }
    if (chunk_in == 0 || p + chunk_in > csize) break;
    memcpy(joined + joined_len, src + p, chunk_in);
    joined_len += chunk_in;
    p += chunk_in;
    inflated += chunk_out;
  }
  MemFile in = {joined, joined_len, 0}, dst = {out, size, 0};
  // window 2^17, no reset interval, 32 KB input buffer, output length known.
  struct lzxd_stream* z = lzxd_init(&g_sys, (struct mspack_file*)&in, (struct mspack_file*)&dst, 17, 0,
                                    0x8000, (off_t)size, 0);
  int result = z ? lzxd_decompress(z, (off_t)size) : -1;
  if (z) lzxd_free(z);
  free(joined);
  return result;
}

static unsigned char* read_all(const char* path, size_t* len) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  *len = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char* buf = malloc(*len);
  if (fread(buf, 1, *len, f) != *len) { free(buf); buf = NULL; }
  fclose(f);
  return buf;
}

// Is id selected by the "all" / "38D,329-32F" list?
static int selected(const char* list, unsigned id) {
  if (strcmp(list, "all") == 0) return 1;
  const char* s = list;
  while (*s) {
    char* end;
    unsigned lo = strtoul(s, &end, 16), hi = lo;
    if (*end == '-') hi = strtoul(end + 1, &end, 16);
    if (id >= lo && id <= hi) return 1;
    s = (*end == ',') ? end + 1 : end;
    if (end == s && *s && *s != ',') break;  // garbage: stop
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc != 5 || (strcmp(argv[1], "pack") && strcmp(argv[1], "tex"))) {
    fprintf(stderr, "usage: pdunpack pack|tex <data file> <ids|all> <out dir>\n");
    return 2;
  }
  const int tex = strcmp(argv[1], "tex") == 0;
  size_t len;
  unsigned char* data = read_all(argv[2], &len);
  if (!data) { fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
  const unsigned count = be32(data);
  const size_t data_start = tex ? 4 + 104 * (size_t)count : 0;  // textures: after both header arrays
  int failures = 0, written = 0;

  // PackedSegFile ids are 1-based (file N = entry N-1); texture numbers are 0-based.
  for (unsigned id = tex ? 0 : 1; id < count + (tex ? 0 : 1); ++id) {
    if (!selected(argv[3], id)) continue;
    const unsigned char* e = tex ? data + 4 + 52 * id : data + 4 + 16 * (id - 1);
    const unsigned offset = be32(e);
    const unsigned size = tex ? be32(e + 20) : be32(e + 4);
    const unsigned csize = tex ? be32(e + 24) : be32(e + 8);
    if (size == 0 || data_start + offset + (csize ? csize : size) > len) continue;

    unsigned char* out = calloc(size, 1);
    int result = 0;
    if (csize) result = xmem_inflate(data + data_start + offset, csize, out, size);
    else memcpy(out, data + data_start + offset, size);  // stored as is
    if (result) {
      fprintf(stderr, "%s 0x%04X: LZX error %d\n", tex ? "tex" : "file", id, result);
      ++failures;
    } else {
      char path[1024];
      snprintf(path, sizeof path, "%s/%s_%04X.bin", argv[4], tex ? "tex" : "file", id);
      FILE* f = fopen(path, "wb");
      if (!f) { fprintf(stderr, "cannot write %s\n", path); free(out); return 1; }
      fwrite(out, 1, size, f);
      fclose(f);
      ++written;
    }
    free(out);
  }
  printf("%d written, %d failed\n", written, failures);
  return failures ? 1 : 0;
}
