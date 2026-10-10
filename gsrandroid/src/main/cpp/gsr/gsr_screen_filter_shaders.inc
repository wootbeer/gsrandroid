// gsr_screen_filter_shaders.inc -- copied verbatim (by script, nothing retyped) from GSRecomp 0.4.3,
// gsrecomp/gbarecomp/src/runtime/screen_filter.cpp: the shared fragment header and the LCD3x, xBR-lv2 and
// CRT Lottes filters, with their authors' licence notices exactly as upstream reproduces them. Included by
// gsr_screen_filter.cpp, which swaps only the "#version 130" line for GLSL ES 3.00. ScaleFX (five passes) is
// not included. After an engine update, copy this block again (see tools/UPSTREAM_UPDATES.md).
//
//   LCD3x      by Gigaherz, public domain.
//   xBR-lv2    by Hyllian, MIT licence (notice below, and licenses/xBR-lv2-MIT.txt).
//   CRT Lottes by Timothy Lottes, public domain.

// Shared by the LCD3x, xBR and CRT Lottes fragment shaders (and ScaleFX's copy). v_uv runs 0..1 across the destination,
// top-left origin, so v_uv * SourceSize is a position in source pixels.
const char* kFragmentCommon =
    "#version 130\n"
    "uniform sampler2D Texture;\n"
    "uniform vec2 SourceSize;\n"
    "uniform vec4 TexRect;\n"
    "uniform vec2 OutputSize;\n"
    "in vec2 v_uv;\n"
    "out vec4 FragColor;\n"
    "\n"
    "// Centre of the 1x pixel containing `pixel`, mapped into TexRect, nearest.\n"
    "vec4 src_texel(vec2 pixel) {\n"
    "  vec2 p = clamp(floor(pixel), vec2(0.0), SourceSize - vec2(1.0));\n"
    "  vec2 t = (p + vec2(0.5)) / SourceSize;\n"
    "  return texture(Texture, mix(TexRect.xy, TexRect.zw, t));\n"
    "}\n";

// LCD3x -- Author: Gigaherz. License: Public domain.
// (libretro glsl-shaders, handheld/shaders/lcd3x.glsl; defaults 16.0 and 4.0)
const char* kLcd3xFragment = R"GLSL(
#define brighten_scanlines 16.0
#define brighten_lcd 4.0

const vec3 offsets = vec3(3.141592654) * vec3(1.0/2.0,1.0/2.0 - 2.0/3.0,1.0/2.0-4.0/3.0);

void main()
{
    vec2 omega = vec2(3.141592654) * vec2(2.0) * SourceSize;
    vec3 res = src_texel(v_uv * SourceSize).xyz;

    vec2 angle = v_uv * omega;

    float yfactor = (brighten_scanlines + sin(angle.y)) / (brighten_scanlines + 1.0);
    vec3 xfactors = (brighten_lcd + sin(angle.x + offsets)) / (brighten_lcd + 1.0);

    vec3 color = yfactor * xfactors * res;

    FragColor = vec4(color.x, color.y, color.z, 1.0);
}
)GLSL";

// xBR-lv2 -- the licence notice below is reproduced verbatim from the upstream
// shader (libretro glsl-shaders, xbr/shaders/xbr-lv2.glsl).
//
//    Hyllian's xBR-lv2 Shader
//
//    Copyright (C) 2011-2016 Hyllian - sergiogdb@gmail.com
//
//    Permission is hereby granted, free of charge, to any person obtaining a copy
//    of this software and associated documentation files (the "Software"), to deal
//    in the Software without restriction, including without limitation the rights
//    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//    copies of the Software, and to permit persons to whom the Software is
//    furnished to do so, subject to the following conditions:
//
//    The above copyright notice and this permission notice shall be included in
//    all copies or substantial portions of the Software.
//
//    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//    THE SOFTWARE.
//
//    Incorporates some of the ideas from SABR shader. Thanks to Joshua Street.
//
// Port notes: the vertex stage's t1..t7 neighbour coordinates are computed in
// the fragment stage as whole-pixel offsets through src_texel(); CORNER_C and
// SMOOTH_TIPS (the upstream defaults) are kept, the "small details" branch
// (default off) and the parameter pragmas are dropped.
const char* kXbrFragment = R"GLSL(
#define XBR_SCALE 3.0
#define XBR_EQ_THRESHOLD 15.0
#define XBR_LV2_COEFFICIENT 2.0
#define lv2_cf XBR_LV2_COEFFICIENT

const vec3 rgbw          = vec3(14.352, 28.176, 5.472);

const vec4 delta   = vec4(1.0/XBR_SCALE, 1.0/XBR_SCALE, 1.0/XBR_SCALE, 1.0/XBR_SCALE);
const vec4 delta_l = vec4(0.5/XBR_SCALE, 1.0/XBR_SCALE, 0.5/XBR_SCALE, 1.0/XBR_SCALE);
const vec4 delta_u = delta_l.yxwz;

const  vec4 Ao = vec4( 1.0, -1.0, -1.0, 1.0 );
const  vec4 Bo = vec4( 1.0,  1.0, -1.0,-1.0 );
const  vec4 Co = vec4( 1.5,  0.5, -0.5, 0.5 );
const  vec4 Ax = vec4( 1.0, -1.0, -1.0, 1.0 );
const  vec4 Bx = vec4( 0.5,  2.0, -0.5,-2.0 );
const  vec4 Cx = vec4( 1.0,  1.0, -0.5, 0.0 );
const  vec4 Ay = vec4( 1.0, -1.0, -1.0, 1.0 );
const  vec4 By = vec4( 2.0,  0.5, -2.0,-0.5 );
const  vec4 Cy = vec4( 2.0,  0.0, -1.0, 0.5 );
const  vec4 Ci = vec4(0.25, 0.25, 0.25, 0.25);

// Difference between vector components.
vec4 df(vec4 A, vec4 B)
{
    return vec4(abs(A-B));
}

// Compare two vectors and return their components are different.
vec4 diff(vec4 A, vec4 B)
{
    return vec4(notEqual(A, B));
}

// Determine if two vector components are equal based on a threshold.
vec4 eq(vec4 A, vec4 B)
{
    return (step(df(A, B), vec4(XBR_EQ_THRESHOLD)));
}

// Determine if two vector components are NOT equal based on a threshold.
vec4 neq(vec4 A, vec4 B)
{
    return (vec4(1.0, 1.0, 1.0, 1.0) - eq(A, B));
}

// Weighted distance.
vec4 wd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h)
{
    return (df(a,b) + df(a,c) + df(d,e) + df(d,f) + 4.0*df(g,h));
}

float c_df(vec3 c1, vec3 c2)
{
      vec3 df = abs(c1 - c2);
      return df.r + df.g + df.b;
}

void main()
{
    vec4 edri, edr, edr_l, edr_u, px; // px = pixel, edr = edge detection rule
    vec4 irlv0, irlv1, irlv2l, irlv2u, block_3d;
    vec4 fx, fx_l, fx_u; // inequations of straight lines.

    vec2 sp  = v_uv * SourceSize;
    vec2 fp  = fract(sp);
    vec2 bp  = floor(sp);

    vec3 A1 = src_texel(bp + vec2(-1.0, -2.0)).xyz;
    vec3 B1 = src_texel(bp + vec2( 0.0, -2.0)).xyz;
    vec3 C1 = src_texel(bp + vec2( 1.0, -2.0)).xyz;
    vec3 A  = src_texel(bp + vec2(-1.0, -1.0)).xyz;
    vec3 B  = src_texel(bp + vec2( 0.0, -1.0)).xyz;
    vec3 C  = src_texel(bp + vec2( 1.0, -1.0)).xyz;
    vec3 D  = src_texel(bp + vec2(-1.0,  0.0)).xyz;
    vec3 E  = src_texel(bp + vec2( 0.0,  0.0)).xyz;
    vec3 F  = src_texel(bp + vec2( 1.0,  0.0)).xyz;
    vec3 G  = src_texel(bp + vec2(-1.0,  1.0)).xyz;
    vec3 H  = src_texel(bp + vec2( 0.0,  1.0)).xyz;
    vec3 I  = src_texel(bp + vec2( 1.0,  1.0)).xyz;
    vec3 G5 = src_texel(bp + vec2(-1.0,  2.0)).xyz;
    vec3 H5 = src_texel(bp + vec2( 0.0,  2.0)).xyz;
    vec3 I5 = src_texel(bp + vec2( 1.0,  2.0)).xyz;
    vec3 A0 = src_texel(bp + vec2(-2.0, -1.0)).xyz;
    vec3 D0 = src_texel(bp + vec2(-2.0,  0.0)).xyz;
    vec3 G0 = src_texel(bp + vec2(-2.0,  1.0)).xyz;
    vec3 C4 = src_texel(bp + vec2( 2.0, -1.0)).xyz;
    vec3 F4 = src_texel(bp + vec2( 2.0,  0.0)).xyz;
    vec3 I4 = src_texel(bp + vec2( 2.0,  1.0)).xyz;

    vec4 b  = vec4(dot(B ,rgbw), dot(D ,rgbw), dot(H ,rgbw), dot(F ,rgbw));
    vec4 c  = vec4(dot(C ,rgbw), dot(A ,rgbw), dot(G ,rgbw), dot(I ,rgbw));
    vec4 d  = b.yzwx;
    vec4 e  = vec4(dot(E,rgbw));
    vec4 f  = b.wxyz;
    vec4 g  = c.zwxy;
    vec4 h  = b.zwxy;
    vec4 i  = c.wxyz;

    vec4 i4, i5, h5, f4;

    i4 = vec4(dot(I4,rgbw), dot(C1,rgbw), dot(A0,rgbw), dot(G5,rgbw));
    i5 = vec4(dot(I5,rgbw), dot(C4,rgbw), dot(A1,rgbw), dot(G0,rgbw));
    h5 = vec4(dot(H5,rgbw), dot(F4,rgbw), dot(B1,rgbw), dot(D0,rgbw));
    f4 = h5.yzwx;

    // These inequations define the line below which interpolation occurs.
    fx   = (Ao*fp.y+Bo*fp.x);
    fx_l = (Ax*fp.y+Bx*fp.x);
    fx_u = (Ay*fp.y+By*fp.x);

    irlv1 = irlv0 = diff(e,f) * diff(e,h);

    // CORNER_C
    irlv1     = (irlv0  * ( neq(f,b) * neq(f,c) + neq(h,d) * neq(h,g) + eq(e,i) * (neq(f,f4) * neq(f,i4) + neq(h,h5) * neq(h,i5)) + eq(e,g) + eq(e,c)) );

    irlv2l = diff(e,g) * diff(d,g);
    irlv2u = diff(e,c) * diff(b,c);

    vec4 fx45i = clamp((fx   + delta   -Co - Ci)/(2.0*delta  ), 0.0, 1.0);
    vec4 fx45  = clamp((fx   + delta   -Co     )/(2.0*delta  ), 0.0, 1.0);
    vec4 fx30  = clamp((fx_l + delta_l -Cx     )/(2.0*delta_l), 0.0, 1.0);
    vec4 fx60  = clamp((fx_u + delta_u -Cy     )/(2.0*delta_u), 0.0, 1.0);

    vec4 wd1, wd2;
    wd1 = wd( e, c,  g, i, h5, f4, h, f);
    wd2 = wd( h, d, i5, f, i4,  b, e, i);

    edri  = step(wd1, wd2) * irlv0;
    edr   = step(wd1 + vec4(0.1, 0.1, 0.1, 0.1), wd2) * step(vec4(0.5, 0.5, 0.5, 0.5), irlv1);
    edr_l = step( lv2_cf*df(f,g), df(h,c) ) * irlv2l * edr;
    edr_u = step( lv2_cf*df(h,c), df(f,g) ) * irlv2u * edr;

    fx45  = edr   * fx45;
    fx30  = edr_l * fx30;
    fx60  = edr_u * fx60;
    fx45i = edri  * fx45i;

    px = step(df(e,f), df(e,h));

    // SMOOTH_TIPS
    vec4 maximos = max(max(fx30, fx60), max(fx45, fx45i));

    vec3 res1 = E;
    res1 = mix(res1, mix(H, F, px.x), maximos.x);
    res1 = mix(res1, mix(B, D, px.z), maximos.z);

    vec3 res2 = E;
    res2 = mix(res2, mix(F, B, px.y), maximos.y);
    res2 = mix(res2, mix(D, H, px.w), maximos.w);

    vec3 res = mix(res1, res2, step(c_df(E, res1), c_df(E, res2)));

    FragColor = vec4(res, 1.0);
}
)GLSL";

// CRT Lottes -- PUBLIC DOMAIN CRT STYLED SCAN-LINE SHADER by Timothy Lottes
// (libretro glsl-shaders, crt/shaders/crt-lottes.glsl). Upstream's header says
// "Please take and use, change, or whatever."
//
// Port notes: parameters keep their upstream default values as #defines; the
// GL_ES / SIMPLE_LINEAR_GAMMA branches and the pragma lines are dropped. Every
// sample goes through src_texel(); reads outside the picture return black, as
// upstream's clamp-to-border wrap does. TextureSize == InputSize here, so the
// upstream size ratios in main() are 1.
const char* kLottesFragment = R"GLSL(
#define hardScan -8.0
#define hardPix -3.0
#define warpX 0.031
#define warpY 0.041
#define maskDark 0.5
#define maskLight 1.5
#define scaleInLinearGamma 1.0
#define shadowMask 3.0
#define brightBoost 1.0
#define hardBloomPix -1.5
#define hardBloomScan -2.0
#define bloomAmount 0.15
#define shape 2.0

//Uncomment to reduce instructions with simpler linearization
//(fixes HD3000 Sandy Bridge IGP)
//#define SIMPLE_LINEAR_GAMMA
#define DO_BLOOM

// ------------- //

// sRGB to Linear.
// Assuming using sRGB typed textures this should not be needed.
float ToLinear1(float c)
{
    if (scaleInLinearGamma == 0.) 
        return c;
    
    return(c<=0.04045) ? c/12.92 : pow((c + 0.055)/1.055, 2.4);
}

vec3 ToLinear(vec3 c)
{
    if (scaleInLinearGamma==0.) 
        return c;
    
    return vec3(ToLinear1(c.r), ToLinear1(c.g), ToLinear1(c.b));
}

// Linear to sRGB.
// Assuming using sRGB typed textures this should not be needed.
float ToSrgb1(float c)
{
    if (scaleInLinearGamma == 0.) 
        return c;
    
    return(c<0.0031308 ? c*12.92 : 1.055*pow(c, 0.41666) - 0.055);
}

vec3 ToSrgb(vec3 c)
{
    if (scaleInLinearGamma == 0.) 
        return c;
    
    return vec3(ToSrgb1(c.r), ToSrgb1(c.g), ToSrgb1(c.b));
}

// Nearest emulated sample given floating point position and texel offset.
// Also zero's off screen.
vec3 Fetch(vec2 pos,vec2 off){
  vec2 px = floor(pos*SourceSize+off);
  // Upstream's texture wrap is clamp-to-border (black): off-screen reads are black.
  if (px.x < 0.0 || px.y < 0.0 || px.x >= SourceSize.x || px.y >= SourceSize.y)
    return vec3(0.0);
  return ToLinear(brightBoost * src_texel(px).rgb);
}

// Distance in emulated pixels to nearest texel.
vec2 Dist(vec2 pos)
{
    pos = pos*SourceSize;
    
    return -((pos - floor(pos)) - vec2(0.5));
}
    
// 1D Gaussian.
float Gaus(float pos, float scale)
{
    return exp2(scale*pow(abs(pos), shape));
}

// 3-tap Gaussian filter along horz line.
vec3 Horz3(vec2 pos, float off)
{
    vec3 b    = Fetch(pos, vec2(-1.0, off));
    vec3 c    = Fetch(pos, vec2( 0.0, off));
    vec3 d    = Fetch(pos, vec2( 1.0, off));
    float dst = Dist(pos).x;

    // Convert distance to weight.
    float scale = hardPix;
    float wb = Gaus(dst-1.0,scale);
    float wc = Gaus(dst+0.0,scale);
    float wd = Gaus(dst+1.0,scale);

    // Return filtered sample.
    return (b*wb+c*wc+d*wd)/(wb+wc+wd);
}

// 5-tap Gaussian filter along horz line.
vec3 Horz5(vec2 pos,float off){
    vec3 a = Fetch(pos,vec2(-2.0, off));
    vec3 b = Fetch(pos,vec2(-1.0, off));
    vec3 c = Fetch(pos,vec2( 0.0, off));
    vec3 d = Fetch(pos,vec2( 1.0, off));
    vec3 e = Fetch(pos,vec2( 2.0, off));
    
    float dst = Dist(pos).x;
    // Convert distance to weight.
    float scale = hardPix;
    float wa = Gaus(dst - 2.0, scale);
    float wb = Gaus(dst - 1.0, scale);
    float wc = Gaus(dst + 0.0, scale);
    float wd = Gaus(dst + 1.0, scale);
    float we = Gaus(dst + 2.0, scale);
    
    // Return filtered sample.
    return (a*wa+b*wb+c*wc+d*wd+e*we)/(wa+wb+wc+wd+we);
}
  
// 7-tap Gaussian filter along horz line.
vec3 Horz7(vec2 pos,float off)
{
    vec3 a = Fetch(pos, vec2(-3.0, off));
    vec3 b = Fetch(pos, vec2(-2.0, off));
    vec3 c = Fetch(pos, vec2(-1.0, off));
    vec3 d = Fetch(pos, vec2( 0.0, off));
    vec3 e = Fetch(pos, vec2( 1.0, off));
    vec3 f = Fetch(pos, vec2( 2.0, off));
    vec3 g = Fetch(pos, vec2( 3.0, off));

    float dst = Dist(pos).x;
    // Convert distance to weight.
    float scale = hardBloomPix;
    float wa = Gaus(dst - 3.0, scale);
    float wb = Gaus(dst - 2.0, scale);
    float wc = Gaus(dst - 1.0, scale);
    float wd = Gaus(dst + 0.0, scale);
    float we = Gaus(dst + 1.0, scale);
    float wf = Gaus(dst + 2.0, scale);
    float wg = Gaus(dst + 3.0, scale);

    // Return filtered sample.
    return (a*wa+b*wb+c*wc+d*wd+e*we+f*wf+g*wg)/(wa+wb+wc+wd+we+wf+wg);
}
  
// Return scanline weight.
float Scan(vec2 pos, float off)
{
    float dst = Dist(pos).y;

    return Gaus(dst + off, hardScan);
}
  
// Return scanline weight for bloom.
float BloomScan(vec2 pos, float off)
{
    float dst = Dist(pos).y;
    
    return Gaus(dst + off, hardBloomScan);
}

// Allow nearest three lines to effect pixel.
vec3 Tri(vec2 pos)
{
    vec3 a = Horz3(pos,-1.0);
    vec3 b = Horz5(pos, 0.0);
    vec3 c = Horz3(pos, 1.0);
    
    float wa = Scan(pos,-1.0); 
    float wb = Scan(pos, 0.0);
    float wc = Scan(pos, 1.0);
    
    return a*wa + b*wb + c*wc;
}
  
// Small bloom.
vec3 Bloom(vec2 pos)
{
    vec3 a = Horz5(pos,-2.0);
    vec3 b = Horz7(pos,-1.0);
    vec3 c = Horz7(pos, 0.0);
    vec3 d = Horz7(pos, 1.0);
    vec3 e = Horz5(pos, 2.0);

    float wa = BloomScan(pos,-2.0);
    float wb = BloomScan(pos,-1.0); 
    float wc = BloomScan(pos, 0.0);
    float wd = BloomScan(pos, 1.0);
    float we = BloomScan(pos, 2.0);

    return a*wa+b*wb+c*wc+d*wd+e*we;
}
  
// Distortion of scanlines, and end of screen alpha.
vec2 Warp(vec2 pos)
{
    pos  = pos*2.0-1.0;    
    pos *= vec2(1.0 + (pos.y*pos.y)*warpX, 1.0 + (pos.x*pos.x)*warpY);
    
    return pos*0.5 + 0.5;
}
  
// Shadow mask.
vec3 Mask(vec2 pos)
{
    vec3 mask = vec3(maskDark, maskDark, maskDark);
  
    // Very compressed TV style shadow mask.
    if (shadowMask == 1.0) 
    {
        float line = maskLight;
        float odd = 0.0;
        
        if (fract(pos.x*0.166666666) < 0.5) odd = 1.0;
        if (fract((pos.y + odd) * 0.5) < 0.5) line = maskDark;  
        
        pos.x = fract(pos.x*0.333333333);

        if      (pos.x < 0.333) mask.r = maskLight;
        else if (pos.x < 0.666) mask.g = maskLight;
        else                    mask.b = maskLight;
        mask*=line;  
    } 

    // Aperture-grille.
    else if (shadowMask == 2.0) 
    {
        pos.x = fract(pos.x*0.333333333);

        if      (pos.x < 0.333) mask.r = maskLight;
        else if (pos.x < 0.666) mask.g = maskLight;
        else                    mask.b = maskLight;
    } 

    // Stretched VGA style shadow mask (same as prior shaders).
    else if (shadowMask == 3.0) 
    {
        pos.x += pos.y*3.0;
        pos.x  = fract(pos.x*0.166666666);

        if      (pos.x < 0.333) mask.r = maskLight;
        else if (pos.x < 0.666) mask.g = maskLight;
        else                    mask.b = maskLight;
    }

    // VGA style shadow mask.
    else if (shadowMask == 4.0) 
    {
        pos.xy  = floor(pos.xy*vec2(1.0, 0.5));
        pos.x  += pos.y*3.0;
        pos.x   = fract(pos.x*0.166666666);

        if      (pos.x < 0.333) mask.r = maskLight;
        else if (pos.x < 0.666) mask.g = maskLight;
        else                    mask.b = maskLight;
    }

    return mask;
}

void main()
{
    // TextureSize == InputSize here, so the upstream size ratios are 1.
    vec2 pos = Warp(v_uv);
    vec3 outColor = Tri(pos);

    //Add Bloom
    outColor.rgb += Bloom(pos)*bloomAmount;

    if (shadowMask > 0.0)
        outColor.rgb *= Mask(gl_FragCoord.xy * 1.000001);
    
    FragColor = vec4(ToSrgb(outColor.rgb), 1.0);
} 
)GLSL";
