// pcf2bdf.cc

/*
 * 詳細は xc/lib/font/bitmap/pcfread.c,pcfwrite.c などを参照のこと
 */


#ifdef WIN32 // Microsoft Visual C++
#define MSVC
#elif _WIN32 // Cygnus GNU Win32 gcc
#define GNUWIN32
#endif


#include <stdio.h>
#include <string.h>
#if defined(MSVC)
#include <io.h>
#include <fcntl.h>
#include <process.h>
#elif defined(GNUWIN32)
#include <sys/fcntl.h>
extern "C" {
int setmode(int handle, int mode);
}
#endif


// miscellaneous definition ///////////////////////////////////////////////////


typedef bool bool8;
typedef unsigned char uint8;
typedef unsigned char byte8;
typedef short int16;
typedef unsigned short uint16;
typedef long int32;
typedef unsigned long uint32;

// 各セクションの ID
enum type32 {
  PCF_PROPERTIES	= (1 << 0),
  PCF_ACCELERATORS	= (1 << 1),
  PCF_METRICS		= (1 << 2),
  PCF_BITMAPS		= (1 << 3),
  PCF_INK_METRICS	= (1 << 4),
  PCF_BDF_ENCODINGS	= (1 << 5),
  PCF_SWIDTHS		= (1 << 6),
  PCF_GLYPH_NAMES	= (1 << 7),
  PCF_BDF_ACCELERATORS	= (1 << 8),
};

// 各セクションのフォーマット
struct format32 {
  uint32 id    :24;	// 下記の 4 つのうちどれか
  uint32 dummy :2;	// = 0 padding
  uint32 scan  :2;	// (1 << scan) バイトごとに bitmap を読む
  uint32 bit   :1;	// 0:LSBit first, 1:MSBit first
  uint32 byte  :1;	// 0:LSByte first, 1:MSByte first
  uint32 glyph :2;	// 文字の 1 ラインは (1 << glyph) バイトアライン
  bool is_little_endien(void) { return !byte; }
};
// format32.id は以下のうちのどれか
#define PCF_DEFAULT_FORMAT     0
#define PCF_INKBOUNDS          2
#define PCF_ACCEL_W_INKBOUNDS  1
#define PCF_COMPRESSED_METRICS 1
// BDF ファイルは, MSBit first かつ MSByte first で出力される
const format32 BDF_format = { PCF_DEFAULT_FORMAT, 0, 0, 1, 1, 0 };

// 文字列か値
union sv {
  char *s;
  int32 v;
};

// フォントのメトリック情報
struct metric_t
{
  int16	leftSideBearing;  // 文字の左端の座標
  int16	rightSideBearing; // 文字の右端の座標
  int16	characterWidth;   // 次の文字までの距離
  int16	ascent;           // Baseline より上のドット数
  int16	descent;          // Baseline より下のドット数
  int16	attributes;
  
  byte8 *bitmaps;         // 文字パターン
  int32 swidth;           // swidth
  sv glyphName;          // この文字の名前
  
  metric_t(void)
  {
    bitmaps = NULL;
    glyphName.s = NULL;
  }
  
  // 文字の幅
  int16 widthBits(void) { return rightSideBearing - leftSideBearing; }
  // 文字の高さ
  int16 height(void) { return ascent + descent; }
  // 文字パターン中で, 横 1 ラインを表すのに何バイト必要か
  int16 widthBytes(format32 f)
  {
    return bytesPerRow(widthBits(), 1 << f.glyph);
  }
  static int16 bytesPerRow(int bits, int nbytes)
  {
    return nbytes == 1 ?  ((bits +  7) >> 3)        // pad to 1 byte
      :    nbytes == 2 ? (((bits + 15) >> 3) & ~1)  // pad to 2 bytes
      :    nbytes == 4 ? (((bits + 31) >> 3) & ~3)  // pad to 4 bytes
      :    nbytes == 8 ? (((bits + 63) >> 3) & ~7)  // pad to 8 bytes
      :    0;
  }
};

#define GLYPHPADOPTIONS 4

#define make_charcode(row,col) (row * 256 + col)
#define NO_SUCH_CHAR (int16)0xffff


// global variables ///////////////////////////////////////////////////////////


// table of contents
int32 nTables;
struct table_t {
  type32 type;		// このセクションの ID
  format32 format;	// このセクションのフォーマット
  int32 size;		// このセクションのサイズ
  int32 offset;		// このセクションのファイル先頭からのオフセット
} *tables;

// properties セクション
int32 nProps;		// プロパティの数
struct props_t {	// プロパティ
  sv name;		// プロパティの名前
  bool8 isStringProp;	// このプロパティが文字列かどうか
  sv value;		// このプロパティの値
} *props;
int32 stringSize;	// string のサイズ
char *string;		// プロパティで使用される文字列

// accelerators セクション
struct accelerators_t {
  bool8	   noOverlap;		/* true if:
				 * max(rightSideBearing - characterWidth) <=
				 * minbounds->metrics.leftSideBearing */
  bool8	   constantMetrics;
  bool8	   terminalFont;	/* true if:
				 * constantMetrics && leftSideBearing == 0 &&
				 * rightSideBearing == characterWidth &&
				 * ascent == fontAscent &&
				 * descent == fontDescent */
  bool8	   constantWidth;	/* true if:
				 * minbounds->metrics.characterWidth
				 * ==
				 * maxbounds->metrics.characterWidth */
  bool8	   inkInside;		/* true if for all defined glyphs:
				 * 0 <= leftSideBearing &&
				 * rightSideBearing <= characterWidth &&
				 * -fontDescent <= ascent <= fontAscent &&
				 * -fontAscent <= descent <= fontDescent */
  bool8	   inkMetrics;		/* ink metrics != bitmap metrics */
  bool8    drawDirection;       /* 0:L->R 1:R->L*/
  int32	   fontAscent;
  int32	   fontDescent;
  int32	   maxOverlap;
  metric_t minBounds;
  metric_t maxBounds;
  metric_t ink_minBounds;
  metric_t ink_maxBounds;
} accelerators;

// metrics セクション
int32 nMetrics;
metric_t *metrics;

// bitmaps セクション
int32 nBitmaps;
uint32 *bitmapOffsets;
uint32 bitmapSizes[GLYPHPADOPTIONS];
byte8 *bitmaps;		// 文字パターン
int32 bitmapSize;	// bitmaps のサイズ

// encodings セクション
int16 firstCol;
int16 lastCol;
int16 firstRow;
int16 lastRow;
int16 defaultCh;	// デフォルト文字
int16 *encodings;	// 文字コードとメトリックの対応
int nEncodings;		// encodings の数
int nValidEncodings;	// encodings の中で有効な文字数

// swidths セクション
int32 nSwidths;

// glyph names セクション
int32 nGlyphNames;
int32 glyphNamesSize;
char *glyphNames;


// other globals
FILE *ifp;		// 入力ファイル
FILE *ofp;		// 出力ファイル
long read_bytes;	// 読み込んだバイト数
format32 format;	// 現在処理中のセクションのフォーマット
metric_t fontbbx;	// フォントのバウンディングボックス
bool verbose;		// メッセージを表示するか


// miscellaneous functions ////////////////////////////////////////////////////


int error_exit(char *str)
{
  fprintf(stderr, "pcf2bdf: %s\n", str);
  exit(1);
  return 1;
}
int error_invalid_exit(char *str)
{
  fprintf(stderr, "pcf2bdf: <%s> invalid PCF file\n", str);
  exit(1);
  return 1;
}

int check_memory(void *ptr)
{
  if (!ptr)
    return error_exit("out of memory");
  return 0;
}


byte8 *read_byte8s(byte8 *mem, size_t size)
{
  size_t read_size =  fread(mem, 1, size, ifp);
  if (read_size != size)
    error_exit("unexpected eof");
  read_bytes += size;
  return mem;
}


char read8(void)
{
  int a = fgetc(ifp);
  read_bytes ++;
  if (a == EOF)
    return (char)error_exit("unexpected eof");
  return (char)a;
}
bool8 read_bool8(void)
{
  return (bool8)!!read8();
}
uint8 read_uint8(void)
{
  return (uint8)read8();
}


int16 make_int16(int a, int b)
{
  int16 value;
  value  = (int16)(a & 0xff) << 8;
  value |= (int16)(b & 0xff);
  return value;
}
int16 read_int16_big(void)
{
  int a = read8();
  int b = read8();
  return make_int16(a, b);
}
int16 read_int16_little(void)
{
  int a = read8();
  int b = read8();
  return make_int16(b, a);
}
int16 read_int16(void)
{
  if (format.is_little_endien())
    return read_int16_little();
  else
    return read_int16_big();
}


int32 make_int32(int a, int b, int c, int d)
{
  int32 value;
  value  = (int32)(a & 0xff) << 24;
  value |= (int32)(b & 0xff) << 16;
  value |= (int32)(c & 0xff) <<  8;
  value |= (int32)(d & 0xff);
  return value;
}
int32 read_int32_big(void)
{
  int a = read8();
  int b = read8();
  int c = read8();
  int d = read8();
  return make_int32(a, b, c, d);
}
int32 read_int32_little(void)
{
  int a = read8();
  int b = read8();
  int c = read8();
  int d = read8();
  return make_int32(d, c, b, a);
}
int32 read_int32(void)
{
  if (format.is_little_endien())
    return read_int32_little();
  else
    return read_int32_big();
}
uint32 read_uint32(void)
{
  return (uint32)read_int32();
}
format32 read_format32_little(void)
{
  int32 v = read_int32_little();
  format32 f;
  f.id     = v >> 8;
  f.dummy  = 0;
  f.scan   = v >> 4;
  f.bit    = v >> 3;
  f.byte   = v >> 2;
  f.glyph  = v >> 0;
  return f;
}


void skip(int n)
{
  for (; 0 < n; n--)
    read8();
}


void bit_order_invert(byte8 *data, int size)
{
  static const byte8 invert[16] =
  { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
  for (int i = 0; i < size; i++)
    data[i] = (invert[data[i] & 15] << 4) | invert[(data[i] >> 4) & 15];
}
void two_byte_swap(byte8 *data, int size)
{
  size &= ~1;
  for (int i = 0; i < size; i += 2)
  {
    byte8 tmp = data[i];
    data[i] = data[i + 1];
    data[i + 1] = tmp;
  }
}
void four_byte_swap(byte8 *data, int size)
{
  size &= ~3;
  for (int i = 0; i < size; i += 4)
  {
    byte8 tmp = data[i];
    data[i] = data[i + 3];
    data[i + 3] = tmp;
    tmp = data[i + 1];
    data[i + 1] = data[i + 2];
    data[i + 2] = tmp;
  }
}


// main ///////////////////////////////////////////////////////////////////////


// type のセクションを探す
bool seek(type32 type)
{
  for (int i = 0; i < nTables; i++)
    if (tables[i].type == type)
    {
      int s = tables[i].offset - read_bytes;
      if (s < 0)
	error_invalid_exit("seek");
      skip(s);
      return true;
    }
  return false;
}


// type のセクションが存在するかどうか
bool is_exist_section(type32 type)
{
  for (int i = 0; i < nTables; i++)
    if (tables[i].type == type)
      return true;
  return false;
}


// メトリック情報を読む
void read_metric(metric_t *m)
{
  m->leftSideBearing  = read_int16();
  m->rightSideBearing = read_int16();
  m->characterWidth   = read_int16();
  m->ascent           = read_int16();
  m->descent          = read_int16();
  m->attributes       = read_int16();
}


// 圧縮されたメトリック情報を読む
void read_compressed_metric(metric_t *m)
{
  m->leftSideBearing  = (int16)read_uint8() - 0x80;
  m->rightSideBearing = (int16)read_uint8() - 0x80;
  m->characterWidth   = (int16)read_uint8() - 0x80;
  m->ascent           = (int16)read_uint8() - 0x80;
  m->descent          = (int16)read_uint8() - 0x80;
  m->attributes       = 0;
}


void verbose_metric(metric_t *m, const char *name)
{
  if (verbose)
  {
    fprintf(stderr, "\t%s.leftSideBearing  = %d\n", name, m->leftSideBearing);
    fprintf(stderr, "\t%s.rightSideBearing = %d\n", name, m->rightSideBearing);
    fprintf(stderr, "\t%s.characterWidth   = %d\n", name, m->characterWidth);
    fprintf(stderr, "\t%s.ascent           = %d\n", name, m->ascent);
    fprintf(stderr, "\t%s.descent          = %d\n", name, m->descent);
    fprintf(stderr, "\t%s.attributes       = %04x\n", name, m->attributes);
  }
}


// accelerators セクションを読む
void read_accelerators(void)
{
  format = read_format32_little();
  if (!(format.id == PCF_DEFAULT_FORMAT ||
	format.id == PCF_ACCEL_W_INKBOUNDS))
    error_invalid_exit("accelerators");
  
  accelerators.noOverlap       = read_bool8();
  accelerators.constantMetrics = read_bool8();
  accelerators.terminalFont    = read_bool8();
  accelerators.constantWidth   = read_bool8();
  accelerators.inkInside       = read_bool8();
  accelerators.inkMetrics      = read_bool8();
  accelerators.drawDirection   = read_bool8();
  /* dummy */ read_bool8();
  accelerators.fontAscent      = read_int32();
  accelerators.fontDescent     = read_int32();
  accelerators.maxOverlap      = read_int32();
  if (verbose)
  {
    fprintf(stderr, "\tnoOverlap       = %d\n", (int)accelerators.noOverlap);
    fprintf(stderr, "\tconstantMetrics = %d\n",
	    (int)accelerators.constantMetrics);
    fprintf(stderr, "\tterminalFont    = %d\n",
	    (int)accelerators.terminalFont);
    fprintf(stderr, "\tconstantWidth   = %d\n",
	    (int)accelerators.constantWidth);
    fprintf(stderr, "\tinkInside       = %d\n", (int)accelerators.inkInside);
    fprintf(stderr, "\tinkMetrics      = %d\n", (int)accelerators.inkMetrics);
    fprintf(stderr, "\tdrawDirection   = %d\n",
	    (int)accelerators.drawDirection);
    fprintf(stderr, "\tfontAscent      = %d\n", (int)accelerators.fontAscent);
    fprintf(stderr, "\tfontDescent     = %d\n", (int)accelerators.fontDescent);
    fprintf(stderr, "\tmaxOverlap      = %d\n", (int)accelerators.maxOverlap);
  }
  read_metric(&accelerators.minBounds);
  read_metric(&accelerators.maxBounds);
  verbose_metric(&accelerators.minBounds, "minBounds");
  verbose_metric(&accelerators.maxBounds, "maxBounds");
  if (format.id == PCF_ACCEL_W_INKBOUNDS)
  {
    read_metric(&accelerators.ink_minBounds);
    read_metric(&accelerators.ink_maxBounds);
    verbose_metric(&accelerators.ink_minBounds, "ink_minBounds");
    verbose_metric(&accelerators.ink_maxBounds, "ink_maxBounds");
  }
  else
  {
    accelerators.ink_minBounds = accelerators.minBounds;
    accelerators.ink_maxBounds = accelerators.maxBounds;
  }
}


// name という名前のプロパティを探し, 文字列プロパティならその値を返す
char *get_property_string(char *name)
{
  for (int i = 0; i < nProps; i++)
  {
    if (strcmp(name, props[i].name.s) == 0)
      if (props[i].isStringProp)
	return props[i].value.s;
      else
	error_invalid_exit("property_string");
  }
  return NULL;
}


// name という名前のプロパティを探し, 数のプロパティならその値を返す
int32 get_property_value(char *name)
{
  for (int i = 0; i < nProps; i++)
  {
    if (strcmp(name, props[i].name.s) == 0)
      if (props[i].isStringProp)
	error_invalid_exit("property_value");
      else
	return props[i].value.v;
  }
  return -1;
}


// name という名前のプロパティが存在するかどうか
bool is_exist_property_value(char *name)
{
  for (int i = 0; i < nProps; i++)
  {
    if (strcmp(name, props[i].name.s) == 0)
      if (props[i].isStringProp)
	return false;
      else
	return true;
  }
  return false;
}


int usage_exit(void)
{
  printf("usage: pcf2bdf [-v] [-o bdf file] [pcf file]\n");
  return 1;
}


int main(int argc, char *argv[])
{
  int i;
  char *ifilename = NULL;
  char *ofilename = NULL;
  
  // オプション解析
  for (i = 1; i < argc; i++)
  {
    if (argv[i][0] == '-')
      if (argv[i][1] == 'v')
	verbose = true;
      else if (i + 1 == argc || argv[i][1] != 'o' || ofilename)
	return usage_exit();
      else
	ofilename = argv[++i];
    else
      if (ifilename)
	return usage_exit();
      else
	ifilename = argv[i];
  }
  if (ifilename)
  {
    ifp = fopen(ifilename, "rb");
    if (!ifp)
      return error_exit("failed to open input pcf file");
  }
  else
  {
#if defined(MSVC)
    _setmode(fileno(stdin), _O_BINARY);
#elif defined(GNUWIN32)
    setmode(fileno(stdin), O_BINARY);
#endif
    ifp = stdin;
  }
  int32 version = read_int32_big();
  if ((version >> 16) == 0x1f9d || // compress'ed
      (version >> 16) == 0x1f8b)    // gzip'ed
  {
    if (!ifilename)
      return error_exit("stdin is gzip'ed or compress'ed\n");
    fclose(ifp);
    char buf[1024];
    sprintf(buf, "gzip -dc %s", ifilename); // TODO
#if defined(MSVC)
    ifp = _popen(buf, "rb");
#else
    ifp = popen(buf, "r");
#if defined(GNUWIN32)
    setmode(fileno(ifp), O_BINARY);
#endif
#endif
    read_bytes = 0;
    if (!ifp)
      return error_exit("failed to execute gzip\n");
  }
  
  if (ofilename)
  {
    ofp = fopen(ofilename, "wb");
    if (!ofp)
      return error_exit("failed to open output bdf file");
  }
  else
    ofp = stdout;
  
  // PCF ファイルを読む ///////////////////////////////////////////////////////
  
  // table of contents を読む
  if (read_bytes == 0)
    version = read_int32_big();
  if (version != make_int32(1, 'f', 'c', 'p'))
    error_exit("this is not PCF file format");
  nTables = read_int32_little();
  check_memory((tables = new table_t[nTables]));
  for (i = 0; i < nTables; i++)
  {
    tables[i].type   = (type32)read_int32_little();
    tables[i].format = read_format32_little();
    tables[i].size   = read_int32_little();
    tables[i].offset = read_int32_little();
  }
  
  // properties セクションを読む
  if (!seek(PCF_PROPERTIES))
    error_exit("PCF_PROPERTIES does not found");
  else
    if (verbose)
      fprintf(stderr, "PCF_PROPERTIES\n");
  format = read_format32_little();
  if (!(format.id == PCF_DEFAULT_FORMAT))
    error_invalid_exit("properties(format)");
  nProps = read_int32();
  check_memory((props = new props_t[nProps]));
  for (i = 0; i < nProps; i++)
  {
    props[i].name.v       = read_int32();
    props[i].isStringProp = read_bool8();
    props[i].value.v      = read_int32();
  }
  skip(3 - (((4 + 1 + 4) * nProps + 3) % 4));
  stringSize = read_int32();
  check_memory((string = new char[stringSize + 1]));
  read_byte8s((byte8 *)string, stringSize);
  string[stringSize] = '\0';
  for (i = 0; i < nProps; i++)
  {
    if (stringSize <= props[i].name.v)
      error_invalid_exit("properties(name)");
    props[i].name.s = string + props[i].name.v;
    if (verbose)
      fprintf(stderr, "\t%s ", props[i].name.s);
    if (props[i].isStringProp)
    {
      if (stringSize <= props[i].value.v)
	error_invalid_exit("properties(value)");
      props[i].value.s = string + props[i].value.v;
      if (verbose)
	fprintf(stderr, "\"%s\"\n", props[i].value.s);
    }
    else
      if (verbose)
	fprintf(stderr, "%ld\n", props[i].value.v);
  }
  
  // 古い accelerators セクションを読む
  if (!is_exist_section(PCF_BDF_ACCELERATORS))
    if (!seek(PCF_ACCELERATORS))
      error_exit("PCF_ACCELERATORS and PCF_BDF_ACCELERATORS do not found");
    else
    {
      if (verbose)
	fprintf(stderr, "PCF_ACCELERATORS\n");
      read_accelerators();
    }
  else
    if (verbose)
      fprintf(stderr, "(PCF_ACCELERATORS)\n");
  
  // metrics セクションを読む
  if (!seek(PCF_METRICS))
    error_exit("PCF_METRICS does not found");
  else
    if (verbose)
      fprintf(stderr, "PCF_METRICS\n");
  format = read_format32_little();
  switch (format.id)
  {
    default:
      error_invalid_exit("metrics");
    case PCF_DEFAULT_FORMAT:
      nMetrics = read_int32();
      check_memory((metrics = new metric_t[nMetrics]));
      for (i = 0; i < nMetrics; i++)
	read_metric(&metrics[i]);
      break;
    case PCF_COMPRESSED_METRICS:
      if (verbose)
	fprintf(stderr, "\tPCF_COMPRESSED_METRICS\n");
      nMetrics = read_int16();
      check_memory((metrics = new metric_t[nMetrics]));
      for (i = 0; i < nMetrics; i++)
	read_compressed_metric(&metrics[i]);
      break;
  }
  if (verbose)
    fprintf(stderr, "\tnMetrics = %ld\n", nMetrics);
  fontbbx = metrics[0];
  for (i = 1; i < nMetrics; i++)
  {
    if (metrics[i].leftSideBearing < fontbbx.leftSideBearing)
      fontbbx.leftSideBearing = metrics[i].leftSideBearing;
    if (fontbbx.rightSideBearing < metrics[i].rightSideBearing)
      fontbbx.rightSideBearing = metrics[i].rightSideBearing;
    if (fontbbx.ascent < metrics[i].ascent)
      fontbbx.ascent = metrics[i].ascent;
    if (fontbbx.descent < metrics[i].descent)
      fontbbx.descent = metrics[i].descent;
  }
  
  // bitmaps セクションを読む
  if (!seek(PCF_BITMAPS))
    error_exit("PCF_BITMAPS does not found");
  else
    if (verbose)
      fprintf(stderr, "PCF_BITMAPS\n");
  format = read_format32_little();
  if (!(format.id == PCF_DEFAULT_FORMAT))
    error_invalid_exit("bitmaps");
  nBitmaps = read_int32();
  check_memory((bitmapOffsets = new uint32[nBitmaps]));
  for (i = 0; i < nBitmaps; i++)
    bitmapOffsets[i] = read_uint32();
  for (i = 0; i < GLYPHPADOPTIONS; i++)
    bitmapSizes[i] = read_uint32();
  bitmapSize = bitmapSizes[format.glyph];
  check_memory((bitmaps = new byte8[bitmapSize]));
  read_byte8s(bitmaps, bitmapSize);
  // 文字パターンの中身を BDF に適合するようにする
  if (verbose)
  {
    fprintf(stderr, "\t1<<format.scan = %d\n", 1 << format.scan);
    fprintf(stderr, "\t%sSBit first\n", format.bit ? "M" : "L");
    fprintf(stderr, "\t%sSByte first\n", format.byte ? "M" : "L");
    fprintf(stderr, "\t1<<format.glyph = %d\n", 1 << format.glyph);
  }
  if (format.bit != BDF_format.bit)
  {
    if (verbose)
      fprintf(stderr, "\tbit_order_invert()\n");
    bit_order_invert(bitmaps, bitmapSize);
  }
  if ((format.bit == format.byte) !=  (BDF_format.bit == BDF_format.byte))
    switch (1 << (BDF_format.bit == BDF_format.byte ?
		  format.scan : BDF_format.scan))
    {
      case 1: break;
      case 2:
	if (verbose)
	  fprintf(stderr, "\ttwo_byte_swap()\n");
	two_byte_swap(bitmaps, bitmapSize);
	break;
      case 4:
	if (verbose)
	  fprintf(stderr, "\tfour_byte_swap()\n");
	four_byte_swap(bitmaps, bitmapSize);
	break;
    }
  // メトリックに文字パターンを関連づける
  for (i = 0; i < nMetrics; i++)
  {
    metric_t &m = metrics[i];
    m.bitmaps = bitmaps + bitmapOffsets[i];
  }
  
  // ink metrics セクションは読まない
  
  // encodings セクションを読む
  if (!seek(PCF_BDF_ENCODINGS))
    error_exit("PCF_BDF_ENCODINGS does not found");
  else
    if (verbose)
      fprintf(stderr, "PCF_ENCODINGS\n");
  format = read_format32_little();
  if (!(format.id == PCF_DEFAULT_FORMAT))
    error_invalid_exit("encoding");
  firstCol  = read_int16();
  lastCol   = read_int16();
  firstRow  = read_int16();
  lastRow   = read_int16();
  defaultCh = read_int16();
  if (verbose)
  {
    fprintf(stderr, "\tfirstCol  = %X\n", firstCol);
    fprintf(stderr, "\tlastCol   = %X\n", lastCol);
    fprintf(stderr, "\tfirstRow  = %X\n", firstRow);
    fprintf(stderr, "\tlastRow   = %X\n", lastRow);
    fprintf(stderr, "\tdefaultCh = %X\n", defaultCh);
  }
  nEncodings = (lastCol - firstCol + 1) * (lastRow - firstRow + 1);
  check_memory((encodings = new int16[nEncodings]));
  for (i = 0; i < nEncodings; i++)
  {
    encodings[i] = read_int16();
    if (encodings[i] != NO_SUCH_CHAR)
      nValidEncodings ++;
  }

  // swidths セクションを読む
  if (seek(PCF_SWIDTHS))
  {
    if (verbose)
      fprintf(stderr, "PCF_SWIDTHS\n");
    format = read_format32_little();
    if (!(format.id == PCF_DEFAULT_FORMAT))
      error_invalid_exit("encoding");
    nSwidths = read_int32();
    if (nSwidths != nMetrics)
      error_exit("nSwidths != nMetrics");
    for (i = 0; i < nSwidths; i++)
      metrics[i].swidth = read_int32();
  }
  else
  {
    if (verbose)
      fprintf(stderr, "no PCF_SWIDTHS\n");
    int32 rx = get_property_value("RESOLUTION_X");
    if (rx <= 0)
      rx = (int)(get_property_value("RESOLUTION") / 100.0 * 72.27) ;
    double p = get_property_value("POINT_SIZE") / 10.0;
    for (i = 0; i < nSwidths; i++)
      metrics[i].swidth =
	(int)(metrics[i].characterWidth / (rx / 72.27) / (p / 1000));
  }
  
  // glyph names セクションを読む
  if (seek(PCF_GLYPH_NAMES))
  {
    if (verbose)
      fprintf(stderr, "PCF_GLYPH_NAMES\n");
    format = read_format32_little();
    if (!(format.id == PCF_DEFAULT_FORMAT))
      error_invalid_exit("encoding");
    nGlyphNames = read_int32();
    if (nGlyphNames != nMetrics)
      error_exit("nGlyphNames != nMetrics");
    for (i = 0; i < nGlyphNames; i++)
      metrics[i].glyphName.v = read_int32();
    glyphNamesSize = read_int32();
    check_memory((glyphNames = new char[glyphNamesSize + 1]));
    read_byte8s((byte8 *)glyphNames, glyphNamesSize);
    glyphNames[glyphNamesSize] = '\0';
    for (i = 0; i < nGlyphNames; i++)
    {
      if (glyphNamesSize <= metrics[i].glyphName.v)
	error_invalid_exit("glyphNames");
      metrics[i].glyphName.s = glyphNames + metrics[i].glyphName.v;
    }
  }
  else
    if (verbose)
      fprintf(stderr, "no PCF_GLYPH_NAMES\n");
  
  // BDF style accelerators セクションを読む
  if (seek(PCF_BDF_ACCELERATORS))
  {
    if (verbose)
      fprintf(stderr, "PCF_BDF_ACCELERATORS\n");
    read_accelerators();
  }
  else
    if (verbose)
      fprintf(stderr, "no PCF_BDF_ACCELERATORS\n");
  
  // write bdf file ///////////////////////////////////////////////////////////
  
  fprintf(ofp, "STARTFONT 2.1\n");
  fprintf(ofp, "FONT %s\n", get_property_string("FONT"));
  int32 rx = get_property_value("RESOLUTION_X");
  int32 ry = get_property_value("RESOLUTION_Y");
  if (!is_exist_property_value("RESOLUTION_X") ||
      !is_exist_property_value("RESOLUTION_Y"))
    rx = ry = (int)(get_property_value("RESOLUTION") / 100.0 * 72.27) ;
  fprintf(ofp, "SIZE %ld %ld %ld\n", get_property_value("PIXEL_SIZE"), rx, ry);
  fprintf(ofp, "FONTBOUNDINGBOX %d %d %d %d\n\n",
	  fontbbx.widthBits(), fontbbx.height(),
	  fontbbx.leftSideBearing, -fontbbx.descent);
  
  int nPropsd = -1;
  if (!is_exist_property_value("DEFAULT_CHAR") &&
      defaultCh != NO_SUCH_CHAR)
    nPropsd ++;
  if (!is_exist_property_value("FONT_DESCENT")) nPropsd ++;
  if (!is_exist_property_value("FONT_ASCENT"))  nPropsd ++;
  if (is_exist_property_value("RESOLUTION_X") &&
      is_exist_property_value("RESOLUTION_Y") &&
      is_exist_property_value("RESOLUTION"))
    nPropsd --;
    
  fprintf(ofp, "STARTPROPERTIES %ld\n", nProps + nPropsd);
  for (i = 0; i < nProps; i++)
  {
    if (strcmp(props[i].name.s, "FONT") == 0)
      continue;
    else if (strcmp(props[i].name.s, "RESOLUTION") == 0 &&
	     is_exist_property_value("RESOLUTION_X") &&
	     is_exist_property_value("RESOLUTION_Y"))
      continue;
    fprintf(ofp, "%s ", props[i].name.s);
    if (props[i].isStringProp)
      fprintf(ofp, "\"%s\"\n", props[i].value.s);
    else
      fprintf(ofp, "%ld\n", props[i].value.v);
  }
  
  if (!is_exist_property_value("DEFAULT_CHAR") &&
      defaultCh != NO_SUCH_CHAR)
    fprintf(ofp, "DEFAULT_CHAR %d\n", defaultCh);
  if (!is_exist_property_value("FONT_DESCENT"))
    fprintf(ofp, "FONT_DESCENT %ld\n", accelerators.fontDescent);
  if (!is_exist_property_value("FONT_ASCENT"))
    fprintf(ofp, "FONT_ASCENT %ld\n", accelerators.fontAscent);
  fprintf(ofp, "ENDPROPERTIES\n\n");
  
  fprintf(ofp, "CHARS %d\n\n", nValidEncodings);
  
  for (i = 0; i < nEncodings; i++)
  {
    if (encodings[i] == NO_SUCH_CHAR)
      continue;
    
    int col = i % (lastCol - firstCol + 1) + firstCol;
    int row = i / (lastCol - firstCol + 1) + firstRow;
    uint16 charcode = make_charcode(row, col);
    metric_t &m = metrics[encodings[i]];
    if (m.glyphName.s)
      fprintf(ofp, "STARTCHAR %s\n", m.glyphName.s);
    else if (0x21 <= charcode && charcode <= 0x7e)
      fprintf(ofp, "STARTCHAR %c\n", (char)charcode);
    else
      fprintf(ofp, "STARTCHAR %04X\n", charcode);
    fprintf(ofp, "ENCODING %d\n", charcode);
    fprintf(ofp, "SWIDTH %ld %d\n", m.swidth, 0);
    fprintf(ofp, "DWIDTH %d %d\n", m.characterWidth, 0);
    fprintf(ofp, "BBX %d %d %d %d\n",
	    m.widthBits(), m.height(), m.leftSideBearing, -m.descent);
    if (0 < m.attributes)
      fprintf(ofp, "ATTRIBUTES %4X\n", (uint16)m.attributes);
    fprintf(ofp, "BITMAP\n");

    int widthBytes = m.widthBytes(format);
    int w = (m.widthBits() + 7) / 8;
    w = w < 1 ? 1 : w;
    byte8 *b = m.bitmaps;
    for (int r = 0; r < m.height(); r++)
    {
      for (int c = 0; c < widthBytes; c++)
      {
	if (c < w)
	  fprintf(ofp, "%02X", *b);
	b++;
      }
      fprintf(ofp, "\n");
    }
    fprintf(ofp, "ENDCHAR\n\n");
  }
  
  fprintf(ofp, "ENDFONT\n");
  return 0;
}
