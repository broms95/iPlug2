/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 D2D backend originally contributed by Joseph Broms

 ==============================================================================
*/


#include <cmath>
#include <algorithm>
#include <sstream>
#include <vector>
#include <algorithm>
#include "IGraphicsD2D.h"
#include <d2d1effects.h>

#ifndef IGRAPHICS_CAIRO

//static RedrawProfiler _redrawProfiler;


using namespace iplug;
using namespace igraphics;

// Direct2D provides native drawing options for many typical shapes,
// otherwise we can fallback to IGraphicsPathBase shape construction
#define USE_NATIVE_SHAPES

#pragma mark - Private Classes and Structs

class d2d_surface_t { };

template<class Interface>
inline void SafeRelease(Interface** ppInterfaceToRelease)
{
  if (*ppInterfaceToRelease != NULL)
  {
    (*ppInterfaceToRelease)->Release();
    (*ppInterfaceToRelease) = NULL;
  }
}

class IGraphicsD2D::Bitmap : public APIBitmap
{
public:
  Bitmap(ID2D1Bitmap* pD2DBitmap, int scale, float drawScale);
  Bitmap(ID2D1Bitmap* pD2DBitmap, int width, int height, int scale, float drawScale);
  virtual ~Bitmap();
};

IGraphicsD2D::Bitmap::Bitmap(ID2D1Bitmap* pD2DBitmap, int scale, float drawScale)
{
  auto size = pD2DBitmap->GetSize();
  SetBitmap(pD2DBitmap, size.width, size.height, scale, drawScale);
}

IGraphicsD2D::Bitmap::Bitmap(ID2D1Bitmap* pD2DBitmap, int width, int height, int scale, float drawScale)
{
  SetBitmap(pD2DBitmap, width, height, scale, drawScale);
}

IGraphicsD2D::Bitmap::~Bitmap()
{
  //TODO: free the bitmap
}

class IGraphicsD2D::Font
{
public:
  Font(IDWriteTextFormat* font, double EMRatio) : mFont(font), mEMRatio(EMRatio) {}
  virtual ~Font() { if (mFont) mFont->Release(); }

  Font(const Font&) = delete;
  Font& operator=(const Font&) = delete;

  IDWriteTextFormat* GetFont() const { return mFont; }
  double GetEMRatio() const { return mEMRatio; }

protected:
  IDWriteTextFormat* mFont;
  double mEMRatio;
};

struct IGraphicsD2D::OSFont : Font
{
  OSFont(const FontDescriptor fontRef, double EMRatio)
    : Font(NULL, EMRatio)
  {}
};

// Fonts
StaticStorage<IGraphicsD2D::Font> IGraphicsD2D::sFontCache;

#pragma mark - Utilites

static inline D2D1_BLEND_MODE D2DBlendMode(const IBlend* pBlend)
{
  //if (!pBlend)
  //{
    return D2D1_BLEND_MODE_MULTIPLY;
  //}
  //switch (pBlend->mMethod)
  //{
  //case EBlend::Default:         // fall through
  //case EBlend::Clobber:         // fall through
  //case EBlend::SourceOver:      return D2D1_BLEND_MODE_MULTIPLY;
  //case EBlend::SourceIn:        return D2D1_BLEND_MODE_MULTIPLY;
  //case EBlend::SourceOut:       return D2D1_BLEND_MODE_MULTIPLY;
  //case EBlend::SourceAtop:      return D2D1_BLEND_MODE_MULTIPLY;
  //case EBlend::DestOver:        return D2D1_BLEND_MODE_MULTIPLY;
  //case EBlend::DestIn:          return D2D1_BLEND_MODE_MULTIPLY;
  //case EBlend::DestOut:         return D2D1_BLEND_MODE_MULTIPLY;
  //case EBlend::DestAtop:        return D2D1_BLEND_MODE_MULTIPLY;
  //case EBlend::Add:             return D2D1_BLEND_MODE_MULTIPLY;
  //case EBlend::XOR:             return D2D1_BLEND_MODE_MULTIPLY;
  //}
}

static inline D2D1_RECT_F D2DRect(const IRECT& bounds)
{
  return D2D1::RectF(bounds.L, bounds.T, bounds.R, bounds.B);
}

static inline D2D1::ColorF D2DColor(const IColor& color)
{
  return D2D1::ColorF(color.R / 255.0f, color.G / 255.0f, color.B / 255.0f, color.A / 255.0f);
}

#pragma mark -

IGraphicsD2D::IGraphicsD2D(IGEditorDelegate& dlg, int w, int h, int fps, float scale)
  : IGraphicsPathBase(dlg, w, h, fps, scale)
{
  DBGMSG("IGraphics Direct2D @ %i FPS\n", fps);
  StaticStorage<Font>::Accessor storage(sFontCache);
  storage.Retain();
}

IGraphicsD2D::~IGraphicsD2D()
{
  D2DFinalize();
  StaticStorage<Font>::Accessor storage(sFontCache);
  storage.Release();
}

void IGraphicsD2D::DrawResize()
{
  SetPlatformContext(nullptr);

  if (mSwapChain)
    D2DResizeSurface();
}

APIBitmap* IGraphicsD2D::LoadAPIBitmap(const char* fileNameOrResID, int scale, EResourceLocation location, const char* ext)
{
  ID2D1Bitmap* pD2DBitmap = nullptr;
  wchar_t fileNameOrResIDWideStr[_MAX_PATH];

  UTF8ToUTF16(fileNameOrResIDWideStr, fileNameOrResID, _MAX_PATH);

  if (location == EResourceLocation::kWinBinary)
  {
    int size = 0;
    //const void* pData = LoadWinResource(fileNameOrResID, "png", size, GetWinModuleHandle());
    HRESULT hr = LoadResourceBitmap(fileNameOrResIDWideStr, L"png", &pD2DBitmap);

  }
  else if (location == EResourceLocation::kAbsolutePath)
  {
    HRESULT hr = LoadBitmapFromFile(fileNameOrResIDWideStr, &pD2DBitmap);

    assert(hr == 0); // TODO: remove
  }

  return new Bitmap(pD2DBitmap, scale, 1.f);
}

APIBitmap* IGraphicsD2D::CreateAPIBitmap(int width, int height, int scale, double drawScale)
{
  ID2D1Bitmap1* pD2DBitmap = nullptr;

  D2D1_PIXEL_FORMAT desc2D = D2D1::PixelFormat();
  desc2D.format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc2D.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;

  // create the bitmap properties --- match the window dpi if we want the drawing operation to scale
  // properly.
  float dpi = GetDpiForWindow(static_cast<HWND>(GetWindow()));
  D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1();
  props.dpiX = dpi;
  props.dpiY = dpi;
  props.pixelFormat = desc2D;
  props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;

  HRESULT hr = mD2DDeviceContext->CreateBitmap(D2D1::SizeU(width, height), 0, 0, props, &pD2DBitmap);

  if (SUCCEEDED(hr))
    return new Bitmap(pD2DBitmap, width, height, scale, drawScale);
  else
    return nullptr;
}

bool IGraphicsD2D::BitmapExtSupported(const char* ext)
{
  char extLower[32];
  ToLower(extLower, ext);
  return (strstr(extLower, "png") != nullptr) /*|| (strstr(extLower, "jpg") != nullptr) || (strstr(extLower, "jpeg") != nullptr)*/;
}

void IGraphicsD2D::PathClear()
{
  // release the path and path sink
  SafeRelease(&mPath);
  SafeRelease(&mPathSink);
}

void IGraphicsD2D::PathClose()
{
  if (mPathSink != NULL)
  {
    // not sure if this is open or close --- for fills it should probably
    // be closed.  I could keep track of the beginning and ending points
    // and check if it is close enough to be closed.
    mPathSink->EndFigure(D2D1_FIGURE_END_CLOSED);
    mPathSink->Close();
    SafeRelease(&mPathSink);
  }
}

void IGraphicsD2D::PathArc(float cx, float cy, float r, float a1, float a2, EWinding winding)
{
  // compute the beginning and ending points
  float a1rad = (a1 - 90) / 180.0f * 3.1415692f;
  float a2rad = (a2 - 90) / 180.0f * 3.1415692f;
  float beginX = cx + std::cos(a1rad) * r;
  float beginY = cy + std::sin(a1rad) * r;
  float endX = cx + std::cos(a2rad) * r;
  float endY = cy + std::sin(a2rad) * r;

  // arc has an implicit lineto/moveto --- TODO: we want to avoid this if beginX, beginY
  // are the same as the current point but right now we don't keep that around
  if (mPathSink == nullptr)
    PathMoveTo(beginX, beginY);
  else
    mPathSink->AddLine(D2D1::Point2F(beginX, beginY));

  // the point is the end point of the arc.
  D2D1_ARC_SEGMENT seg;
  seg.point.x = endX;
  seg.point.y = endY;
  seg.size.width = r;
  seg.size.height = r;
  seg.rotationAngle = fmodf(a2 - a1, 360);
  seg.sweepDirection = winding == EWinding::CW ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
  seg.arcSize = (((a2 - a1) < 180) ? D2D1_ARC_SIZE_SMALL : D2D1_ARC_SIZE_LARGE);
  mPathSink->AddArc(seg);
}

void IGraphicsD2D::PathMoveTo(float x, float y)
{
  if (mPathSink == nullptr)
  {
    // start a new path
    SafeRelease(&mPath);
    mFactory->CreatePathGeometry(&mPath);
    mPath->Open(&mPathSink);
    mPathSink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);  // TODO: might need to be different
    mPathSink->BeginFigure(D2D1::Point2F(x, y), D2D1_FIGURE_BEGIN_FILLED);
    mInFigure = true;
  }
  else
  {
    if (mInFigure)
    {
      mPathSink->EndFigure(D2D1_FIGURE_END_CLOSED);
      mPathSink->Close();
      mInFigure = false;
    }
  }
}

void IGraphicsD2D::PathLineTo(float x, float y)
{
  if (mPathSink != nullptr)
  {
    mPathSink->AddLine(D2D1::Point2F(x, y));
  }
}

void IGraphicsD2D::PathCubicBezierTo(float c1x, float c1y, float c2x, float c2y, float x2, float y2)
{
  if (mPathSink != nullptr)
  {
    D2D1_BEZIER_SEGMENT seg;
    seg.point1.x = c1x;
    seg.point1.y = c1y;
    seg.point2.x = c2x;
    seg.point2.y = c2y;
    seg.point3.x = x2;
    seg.point3.y = y2;
    mPathSink->AddBezier(seg);
  }
}

void IGraphicsD2D::PathQuadraticBezierTo(float cx, float cy, float x2, float y2)
{
  if (mPathSink != nullptr)
  {
    D2D1_QUADRATIC_BEZIER_SEGMENT seg;
    seg.point1.x = cx;
    seg.point1.y = cy;
    seg.point2.x = x2;
    seg.point2.y = y2;
    mPathSink->AddQuadraticBezier(seg);
  }
}

void IGraphicsD2D::RenderCheck()
{
  return;
  if (!mInDraw)
  {
    DBGMSG("Do not access outside of mInDraw");
  }
}


void IGraphicsD2D::PathStroke(const IPattern& pattern, float thickness, const IStrokeOptions& options, const IBlend* pBlend)
{
  // close it open if necessary
  if (mPathSink != nullptr)
  {
    mPathSink->EndFigure(D2D1_FIGURE_END_OPEN);
    mPathSink->Close();
    mInFigure = false;
    SafeRelease(&mPathSink);
  }
  RenderCheck();
  mD2DDeviceContext->DrawGeometry(mPath, GetBrush(pattern.GetStop(0).mColor), thickness);
}

void IGraphicsD2D::PathFill(const IPattern& pattern, const IFillOptions& options, const IBlend* pBlend)
{
  if (mPathSink != NULL)
  {
    mPathSink->EndFigure(D2D1_FIGURE_END_CLOSED);
    mPathSink->Close();
    mInFigure = false;
    SafeRelease(&mPathSink);
  }
  RenderCheck();
  mD2DDeviceContext->FillGeometry(mPath, GetBrush(pattern.GetStop(0).mColor));
}

void IGraphicsD2D::DrawLine(const IColor& color, float x1, float y1, float x2, float y2, const IBlend* pBlend, float thickness)
{
#ifdef USE_NATIVE_SHAPES
  PathClear();
  RenderCheck();
  mD2DDeviceContext->DrawLine(D2D1::Point2F(x1,y1), D2D1::Point2F(x2, y2), GetBrush(color), thickness);
#else
  IGraphicsPathBase::DrawLine(color, x1, y1, x2, y2, pBlend, thickness);
#endif
}

void IGraphicsD2D::DrawRect(const IColor& color, const IRECT& bounds, const IBlend* pBlend, float thickness)
{
#ifdef USE_NATIVE_SHAPES
  PathClear();
  RenderCheck();
  mD2DDeviceContext->DrawRectangle(D2DRect(bounds), GetBrush(color));
#else
  IGraphicsPathBase::DrawRect(color, bounds, pBlend, thickness);
#endif
}

void IGraphicsD2D::DrawRoundRect(const IColor& color, const IRECT& bounds, float cornerRadius, const IBlend* pBlend, float thickness)
{
#ifdef USE_NATIVE_SHAPES
  PathClear();
  D2D1_ROUNDED_RECT rr;
  rr.radiusX = cornerRadius;
  rr.radiusY = cornerRadius;
  rr.rect = D2DRect(bounds);
  RenderCheck();
  mD2DDeviceContext->DrawRoundedRectangle(rr, GetBrush(color));
#else
  IGraphicsPathBase::DrawRoundRect(color, bounds, cornerRadius, pBlend, thickness);
#endif
}

void IGraphicsD2D::FillRect(const IColor& color, const IRECT& bounds, const IBlend* pBlend)
{
  RenderCheck();
#ifdef USE_NATIVE_SHAPES
  PathClear();
  mD2DDeviceContext->FillRectangle(D2DRect(bounds), GetBrush(color));
#else
  IGraphicsPathBase::FillRect(color, bounds, pBlend);
#endif
}

void IGraphicsD2D::FillRoundRect(const IColor& color, const IRECT& bounds, float cornerRadius, const IBlend* pBlend)
{
  RenderCheck();
#ifdef USE_NATIVE_SHAPES
  PathClear();
  D2D1_ROUNDED_RECT rr;
  rr.radiusX = cornerRadius;
  rr.radiusY = cornerRadius;
  rr.rect = D2DRect(bounds);
  mD2DDeviceContext->FillRoundedRectangle(rr, GetBrush(color));
#else
  IGraphicsPathBase::FillRoundRect(color, bounds, cornerRadius, pBlend);
#endif
}

void IGraphicsD2D::FillCircle(const IColor& color, float cx, float cy, float r, const IBlend* pBlend)
{
  RenderCheck();
#ifdef USE_NATIVE_SHAPES
  PathClear();
  D2D1_ELLIPSE shape;
  shape.radiusX = r;
  shape.radiusY = r;
  shape.point.x = cx;
  shape.point.y = cy;
  mD2DDeviceContext->FillEllipse(shape, GetBrush(color));
#else
  IGraphicsPathBase::FillCircle(color, cx, cy, r, pBlend);
#endif
}

void IGraphicsD2D::FillEllipse(const IColor& color, const IRECT& bounds, const IBlend* pBlend)
{
  RenderCheck();
  // TODO: support angle do the rotation here -- we can't rotate the path on its own easily.
#ifdef USE_NATIVE_SHAPES
  PathClear();
  D2D1_ELLIPSE shape;
  shape.radiusX = bounds.W() / 2.0f;
  shape.radiusY = bounds.H() / 2.0f;
  shape.point.x = bounds.MW();
  shape.point.y = bounds.MH();
  mD2DDeviceContext->FillEllipse(shape, GetBrush(color));
#else
  // TODO: doesn't work because path ellipse is broken
  IGraphicsPathBase::FillEllipse(color, bounds, pBlend);
#endif
}

void IGraphicsD2D::FillEllipse(const IColor& color, float x, float y, float r1, float r2, float angle, const IBlend* pBlend)
{
  RenderCheck();
  // TODO: support angle do the rotation here -- we can't rotate the path on its own easily.
#ifdef USE_NATIVE_SHAPES
  PathClear();
  D2D1_ELLIPSE shape;
  shape.radiusX = r1;
  shape.radiusY = r2;
  shape.point.x = x;
  shape.point.y = y;
  mD2DDeviceContext->FillEllipse(shape, GetBrush(color));
#else
  // TODO: doesn't work because path ellipse is broken
  IGraphicsPathBase::FillEllipse(color, bounds, pBlend);
#endif
}

void IGraphicsD2D::GetLayerBitmapData(const ILayerPtr& layer, RawBitmapData& data)
{
  const APIBitmap* pBitmap = layer->GetAPIBitmap();

  int width = pBitmap->GetWidth();
  int height = pBitmap->GetHeight();

  IWICBitmap* pWICBitmap = nullptr;
  mWICFactory.Get()->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &pWICBitmap);

  ID2D1RenderTarget* pWICRenderTarget = nullptr;
  mFactory->CreateWicBitmapRenderTarget(pWICBitmap,
    D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
    0.f, 0.f, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE),
    &pWICRenderTarget);

  WICRect rect = { 0, 0, width, height };
  IWICBitmapLock* pWICLock = 0;
  pWICBitmap->Lock(&rect, WICBitmapLockRead, &pWICLock);

  BYTE* pBytes = 0;
  UINT size = 0;
  pWICLock->GetDataPointer(&size, &pBytes);

  data.Resize(size);

  if (data.GetSize() >= size)
    memcpy(data.Get(), pBytes, size);
}

void IGraphicsD2D::ApplyShadowMask(ILayerPtr& layer, RawBitmapData& mask, const IShadow& shadow)
{
//  mD2DDeviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
//
//  auto layerBitmap = layer->GetAPIBitmap()->GetBitmap();
//
//  ID2D1BitmapRenderTarget* pCompatibleRenderTarget = NULL;
//  mD2DDeviceContext->CreateCompatibleRenderTarget(layerBitmap->GetSize(), &pCompatibleRenderTarget);
//  layerBitmap->CopyFromRenderTarget(nullptr, pCompatibleRenderTarget, nullptr);
//
//  pCompatibleRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
//
//  pCompatibleRenderTarget->FillOpacityMask()
//    m_pBitmapMask,
//    m_pOriginalBitmapBrush,
//    D2D1_OPACITY_MASK_CONTENT_GRAPHICS,
//    &rcBrushRect,
//    NULL
//  );
//
//  m_pRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void IGraphicsD2D::DrawBitmap(const IBitmap& bitmap, const IRECT& dest, int srcX, int srcY, const IBlend* pBlend)
{
  // example code
/*  cairo_save(mContext);
  cairo_rectangle(mContext, dest.L, dest.T, dest.W(), dest.H());
  cairo_clip(mContext);
  cairo_surface_t* surface = bitmap.GetAPIBitmap()->GetBitmap();
  cairo_set_source_surface(mContext, surface, dest.L - srcX, dest.T - srcY);
  cairo_set_operator(mContext, CairoBlendMode(pBlend));
  cairo_paint_with_alpha(mContext, BlendWeight(pBlend));
  cairo_restore(mContext);
  */

  ID2D1Bitmap* b = bitmap.GetAPIBitmap()->GetBitmap();
  auto bitmapSize = b->GetPixelSize();
  float dstDpiX, dstDpiY;
  float srcDpiX, srcDpiY;
  mD2DDeviceContext->GetDpi(&dstDpiX, &dstDpiY);
  b->GetDpi(&srcDpiX, &srcDpiY);


  double scale = 1.0f;
  if (!mLayers.empty())
  {
//    scale = bitmap.GetDrawScale();
  }
  else
  {
//    scale = bitmap.GetScale();
  }
  scale = bitmap.GetDrawScale() * bitmap.GetScale();

//  if (mLayers.empty())
  {
//    scale = bitmap.GetScale();
//    scale = bitmap.GetDrawScale();
//    scale = bitmap.GetScale()*bitmap.GetDrawScale();
//    scale = dstDpiX / srcDpiX;
//        scale = bitmap.GetScale()*bitmap.GetDrawScale();
  }
//  scale = dstDpiX / srcDpiX;


  double scale1 = 1.0 / (bitmap.GetScale() * bitmap.GetDrawScale());
  double scale2 = bitmap.GetScale() * bitmap.GetDrawScale();

  RenderCheck();
  float sX = srcX;
  float sY = srcY;
  D2D1_RECT_F srcRect = D2D1::RectF(
    static_cast<float>(sX * scale),
    static_cast<float>(sY * scale),
    static_cast<float>(sX + dest.W() * scale),
    static_cast<float>(sY + dest.H()) * scale);
  D2D1_RECT_F dstRect = D2DRect(dest);




  // get the bitmap status just to see
  D2D1_SIZE_F size = b->GetSize();
  DBGMSG("DrawBitmap");
  DBGMSG("  scale %f %f", scale1, scale2);
  DBGMSG("  rect src %f %f %f %f (dpi %f %f)", srcRect.left, srcRect.top, srcRect.right, srcRect.bottom, srcDpiX, srcDpiY);
  DBGMSG("  rect dst %f %f %f %f (dpi %f %f)", dstRect.left, dstRect.top, dstRect.right, dstRect.bottom, dstDpiX, dstDpiY);
  mD2DDeviceContext->DrawBitmap(b, &dstRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &srcRect);
  if (mLayers.empty())
    mD2DDeviceContext->DrawRectangle(dstRect, GetBrush(IColor(255, 0, 255, 0)), 2.0f);
  else
    mD2DDeviceContext->DrawRectangle(dstRect, GetBrush(IColor(255, 255, 0, 0)), 2.0f);
}

IColor IGraphicsD2D::GetPoint(int x, int y)
{
  //TODO
  return IColor();
}

std::wstring s2ws(const std::string& str)
{
  int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
  std::wstring wstrTo(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
  return wstrTo;
}

void IGraphicsD2D::PrepareAndMeasureText(const IText& text, const char* str, IRECT& r, double& x, double& y, IDWriteTextFormat*& font) const
{
  // not needed right now because we really need to cache IDWriteTextFormat per size requested --- it would be in a different cache
  StaticStorage<Font>::Accessor storage(sFontCache);
  Font* pCachedFont = storage.Find(text.mFont);
  assert(pCachedFont && "No font found - did you forget to load it?");

  std::string fontName(text.mFont);
  
/*  HRESULT hr = m_pDWriteFactory_->CreateTextFormat(
    s2ws(fontName).data(), NULL,
    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
    text.mSize * pCachedFont->GetEMRatio(),
    L"en-us", &font);
  font->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  */
  IGraphicsD2D* constThis = const_cast<IGraphicsD2D*>(this);
  font = constThis->GetFont(text.mFont, text.mSize * pCachedFont->GetEMRatio());

  // we don't have text wrapping, but let's make the max sufficiently big.
  std::wstring strString(s2ws(str));
  IDWriteTextLayout1* layout = nullptr;
  HRESULT hr = mDWriteFactory->CreateTextLayout(
    strString.data(), // The string to be laid out and formatted.
    strString.length(), // The length of the string.
    font, // The text format to apply to the string (contains font information, etc).
    r.W(),// The width of the layout box.
    r.H(),// The height of the layout box.
    (IDWriteTextLayout**)&layout);	// The IDWriteTextLayout interface pointer.

  DWRITE_TEXT_METRICS metrics;
  DWRITE_TEXT_ALIGNMENT textAlign = DWRITE_TEXT_ALIGNMENT_CENTER;
  DWRITE_PARAGRAPH_ALIGNMENT paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;

  hr = layout->GetMetrics(&metrics);

  switch (text.mAlign)
  {
    case EAlign::Near:     textAlign = DWRITE_TEXT_ALIGNMENT_LEADING;     x = r.L;                          break;
    case EAlign::Center:   textAlign = DWRITE_TEXT_ALIGNMENT_CENTER;      x = static_cast<double>(r.MW() - (metrics.width/2.f)); break;
    case EAlign::Far:      textAlign = DWRITE_TEXT_ALIGNMENT_TRAILING;    x = r.R - metrics.width;          break;
  }

  switch (text.mVAlign)
  {
    case EVAlign::Top:     paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;     y = r.T;                               break;
    case EVAlign::Middle:  paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;   y = static_cast<double>(r.MH() - (metrics.height / 2.f));   break;
    case EVAlign::Bottom:  paraAlign = DWRITE_PARAGRAPH_ALIGNMENT_FAR;      y = r.B - metrics.height;              break;
  }

  layout->SetTextAlignment(textAlign);
  layout->SetParagraphAlignment(paraAlign);

  r = IRECT(x, y, x + metrics.width, y + metrics.height);

  SafeRelease(&layout);
}

void IGraphicsD2D::DoMeasureText(const IText& text, const char* str, IRECT& bounds) const
{
  IRECT r = bounds;
  IDWriteTextFormat* format;
  double x, y;
  PrepareAndMeasureText(text, str, bounds, x, y, format);
  DoMeasureTextRotation(text, r, bounds);
//  format->Release();
}

void IGraphicsD2D::DoDrawText(const IText& text, const char* str, const IRECT& bounds, const IBlend* pBlend)
{
  RenderCheck();
  IRECT measured = bounds;
  IDWriteTextFormat* format;
  double x, y;

  const IColor& c = text.mFGColor;

  PrepareAndMeasureText(text, str, measured, x, y, format);
  PathTransformSave();
  DoTextRotation(text, bounds, measured);

  std::wstring strString(s2ws(str));
  D2D1_RECT_F pos = D2DRect(bounds);
  pos.left = x;
  pos.top = y;
  pos.right = x + bounds.W();
  pos.bottom = y + bounds.H();
  mD2DDeviceContext->DrawTextA(strString.data(), strString.length(), format, pos, GetBrush(text.mFGColor));
    //(accidental.GetBuffer(), accidental.GetLength(), accidentalFont, incidentalBounds, m_brightTextBrush);

  PathTransformRestore();
}

void IGraphicsD2D::SetPlatformContext(void* pContext)
{
  // initialize if not already
  if (!mFactory && pContext)
    D2DInitialize();

  IGraphics::SetPlatformContext(pContext);
}

/*
void IGraphicsD2D::Draw(IRECTList& rects)
{
  // The Direct2D backend does its own version of Draw
  // because we need to keep track to the dirty rects
  // to pass to Present1 at the end of EndFrame.
  if (!rects.Size())
    return;

  float scale = GetBackingPixelScale();

  BeginFrame();


  if (mStrict)
  {
    IRECT r = rects.Bounds();
    r.PixelAlign(scale);
    Draw(r, scale);
  }
  else

  {
    rects.PixelAlign(scale);
    rects.Optimize();

    for (auto i = 0; i < rects.Size(); i++)
      Draw(rects.Get(i), scale);
  }

  EndFrame();
}
*/

void IGraphicsD2D::BeginFrame()
{
  IGraphicsPathBase::BeginFrame();

  // lazy create d2d resources if necessary
  if (!mD2DDeviceContext)
  {
    D2DCreateDevice();
    D2DCreateDeviceSwapChainBitmap();
//    D2DCreateDeviceResources();
  }

  // check for occlusion
//  if (SUCCEEDED(hr) && !(m_mD2DDeviceContext->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED))


//  assert(mInDraw == false);
//  mInDraw = true;
//  mD2DDeviceContext->BeginDraw();

  mTargetSize = mD2DDeviceContext->GetSize();
  mLayerTransform = D2D1::Matrix3x2F::Scale(mTargetSize.width / Width(), mTargetSize.height / Height());
  PathTransformReset();

//  D2D1::Matrix3x2F mat = D2D1::Matrix3x2F::Scale(mTargetSize.width / Width(), mTargetSize.height / Height());
//  mD2DDeviceContext->SetTransform(mat);

  // this is just for testing if the entire background is redrawn.
//  mD2DDeviceContext->Clear(D2D1::ColorF(D2D1::ColorF::Green));
//  mD2DDeviceContext->DrawRectangle(D2D1::RectF(100,100,200,200), GetBrush(IColor(255, 255, 0, 0)));
}

void IGraphicsD2D::EndFrame()
{
  HRESULT hr = S_OK;

  if (mInDraw)
  {
    // pop off the clipping region
    if (mPushClipCalled)
    {
      mD2DDeviceContext->PopAxisAlignedClip();
      mPushClipCalled = false;
    }
 
    mInDraw = false;

    // garbage collect now since this is optimal time to do CPU work while
    // waiting for the GPU to complete.
    GarbageCollectFontCache(100);

    // finishes everything
    hr = mD2DDeviceContext->EndDraw();
    mInDraw = false;
    if (hr != S_OK)
    {
      DBGMSG("failed enddraw");
    }

//    _redrawProfiler.StopDrawingOperation();


    DXGI_FRAME_STATISTICS stats;
    mSwapChain->GetFrameStatistics(&stats);

    // compute the time left in the vsync
    LARGE_INTEGER qpcFreq;
    LARGE_INTEGER qpcTime;
    ::QueryPerformanceFrequency(&qpcFreq);
    ::QueryPerformanceCounter(&qpcTime);
    double t = ((double)qpcTime.QuadPart - (double)stats.SyncQPCTime.QuadPart)/ (double)qpcFreq.QuadPart;
//    DBGMSG("left\t%lf", t);


/*
    if (mLastVsync != 0)
    {
      // see if the vsync is now one greater
      if (mLastVsync + 1 < stats.PresentRefreshCount)
      {
        int framesMissed = stats.PresentRefreshCount - mLastVsync - 1;
        DBGMSG("Missed %i vsyncs after %i good vsyncs", framesMissed, mFramesGood);
        mFramesGood = 0;
      }
      else
      {
        mFramesGood++;
      }
    }
    mLastVsync = stats.PresentRefreshCount;
//    DBGMSG("%i %i %i", stats.PresentCount, stats.SyncRefreshCount, stats.PresentRefreshCount);
*/

    // present to screen
    DXGI_PRESENT_PARAMETERS presentParameters = { 0 };
    presentParameters.DirtyRectsCount = mDrawnRects.Size();
    RECT* presentRects = (RECT*)malloc(sizeof(RECT)*mDrawnRects.Size());
    for (int i = 0; i < mDrawnRects.Size(); i++)
    {
      auto& r = mDrawnRects.Get(i);

      // we need to clip to bounderies of the window to the swap chain will
      // fail.
      presentRects[i].left   = Clip((LONG)r.L, swapChainRect.left, swapChainRect.right);
      presentRects[i].top    = Clip((LONG)r.T, swapChainRect.top, swapChainRect.bottom);
      presentRects[i].right  = Clip((LONG)r.R, swapChainRect.left, swapChainRect.right);
      presentRects[i].bottom = Clip((LONG)r.B, swapChainRect.top, swapChainRect.bottom);
    }
    presentParameters.pDirtyRects = presentRects;

    hr = mSwapChain->Present1(0, 0, &presentParameters);
    if (hr == DXGI_ERROR_INVALID_CALL)
    {
      // probably not handling resize properly
      DBGMSG("Present failed --- invalid call");
    }
    else if (hr == DXGI_STATUS_OCCLUDED)
    {
      // the window content is not visible.
      DBGMSG("Present failed --- occulued");
    }
    else if (S_OK != hr)
    {
      D2DReleaseDevice();
    }

    free(presentRects);

    // start over again
    mD2DDeviceContext->BeginDraw();
    mInDraw = true;



    IGraphicsPathBase::EndFrame();
  }
}

bool IGraphicsD2D::LoadAPIFont(const char* fontID, const PlatformFontPtr& font)
{
  StaticStorage<Font>::Accessor storage(sFontCache);

  if (storage.Find(fontID))
    return true;

  IFontDataPtr data = font->GetFontData();

  if (!data->IsValid())
    return false;

  std::unique_ptr<OSFont> d2dFont(new OSFont(font->GetDescriptor(), data->GetHeightEMRatio()));
  storage.Add(d2dFont.release(), fontID);
  return true;
}

void IGraphicsD2D::PathTransformSetMatrix(const IMatrix& m)
{
  if (!mInDraw) return;
  D2D1::Matrix3x2F finalMat =
    D2D1::Matrix3x2F(m.mXX, -m.mXY, -m.mYX, m.mYY, m.mTX, m.mTY) *
    mLayerTransform;
  mD2DDeviceContext->SetTransform(finalMat);
}

void IGraphicsD2D::SetClipRegion(const IRECT& r)
{
  if (!mInDraw) return;
//  DBGMSG("SetClipRegion(%f,%f,%f,%f)", r.L, r.T, r.R, r.B);
  if (mPushClipCalled)
    mD2DDeviceContext->PopAxisAlignedClip();
  
  mD2DDeviceContext->PushAxisAlignedClip(D2DRect(r), D2D1_ANTIALIAS_MODE_ALIASED);
  mPushClipCalled = true;
}

void IGraphicsD2D::UpdateLayer()
{
  if (mInDraw)
  {
    // pop off the clipping region
    if (mPushClipCalled)
    {
      mD2DDeviceContext->PopAxisAlignedClip();
      mPushClipCalled = false;
    }

    HRESULT hr = mD2DDeviceContext->EndDraw();
    mInDraw = false;
    if (hr != S_OK)
    {
      DBGMSG("Issue with enddraw");
    }
  }

  // setup the current render context to either the swapchain bitmap or an offscreen bitmap.
  if (mLayers.empty())
  {
    assert(mD2DDeviceContext);
    assert(mSwapChainBitmap);
    mD2DDeviceContext->SetTarget(mSwapChainBitmap);

    //@@@
    mD2DDeviceContext->BeginDraw();
    mInDraw = true;
    mTargetSize = mD2DDeviceContext->GetSize();
    mLayerTransform = D2D1::Matrix3x2F::Scale(mTargetSize.width / Width(), mTargetSize.height / Height());
    PathTransformReset();

  }
  else
  {
    // use the top bitmap
    ID2D1Bitmap* bitmap = mLayers.top()->GetAPIBitmap()->GetBitmap();
    mD2DDeviceContext->SetTarget(bitmap);

    // there is an implicit begin draw whenever switching layers to a bitmap
    mD2DDeviceContext->BeginDraw();
    mInDraw = true;

    IRECT bounds = mLayers.top()->Bounds();
    mLayerTransform = D2D1::Matrix3x2F::Translation(-bounds.L, -bounds.T);

    PathTransformReset();
  }
}



void IGraphicsD2D::D2DInitialize()
{
  HRESULT hr = 0;
  hr = CoInitialize(NULL);

  D2D1_FACTORY_OPTIONS options;
  ZeroMemory(&options, sizeof(D2D1_FACTORY_OPTIONS));
  hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, reinterpret_cast<void**>(&mFactory));
  hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory1), reinterpret_cast<IUnknown**>(&mDWriteFactory));

  // create WIC factory
  hr = CoCreateInstance(CLSID_WICImagingFactory2, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory2, &mWICFactory);

  D2DCreateFactoryResources();

  if (!mD2DDeviceContext)
  {
    D2DCreateDevice();
    D2DCreateDeviceSwapChainBitmap();
  }
}

void IGraphicsD2D::D2DFinalize()
{
  if (mD2DDeviceContext)
  {
    D2DReleaseDevice();
  }

  D2DReleaseFactoryResources();

  SafeRelease(&mFactory);
  SafeRelease(&mDWriteFactory);

  CoUninitialize();
}

HRESULT IGraphicsD2D::D2DCreateDeviceHelper(D3D_DRIVER_TYPE const type, ID3D11Device*& device)
{
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
//  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
  return D3D11CreateDevice(nullptr,
    type,
    nullptr,
    flags,
    nullptr,
    0,
    D3D11_SDK_VERSION,
    &device,
    nullptr,
    nullptr);
}

void IGraphicsD2D::D2DCreateDevice()
{
  HWND hWnd = (HWND)GetWindow();

  HRESULT hr = S_OK;
  IDXGIDevice* dxgi = nullptr;
  IDXGIAdapter* dxadapt = nullptr;
  IDXGIFactory2* dxgiFactory = nullptr;

  bool softwareOnly = false;

  if (!softwareOnly)
    hr = D2DCreateDeviceHelper(D3D_DRIVER_TYPE_HARDWARE, mD3DDevice);

  if (DXGI_ERROR_UNSUPPORTED == hr || softwareOnly)
    hr = D2DCreateDeviceHelper(D3D_DRIVER_TYPE_WARP, mD3DDevice);

  mD3DDevice->QueryInterface<IDXGIDevice>(&dxgi);
  hr = dxgi->GetAdapter(&dxadapt);
  hr = dxadapt->GetParent(__uuidof(IDXGIFactory2), (void**)&dxgiFactory);

  hr = mFactory->CreateDevice(dxgi, &mD2DDevice);
  hr = mD2DDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &mD2DDeviceContext);

  hr = mD3DDevice->QueryInterface<IDXGIDevice>(&dxgi);
  hr = dxgi->GetAdapter(&dxadapt);
  hr = dxadapt->GetParent(__uuidof(IDXGIFactory2), (void**)&dxgiFactory);

  DXGI_SWAP_CHAIN_DESC1 props = {};
  /*	props.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.SampleDesc.Count = 1;
    props.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    props.BufferCount = 2;
    */

  props.Width = 0;
  props.Height = 0;
  props.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  props.Stereo = false;
  props.SampleDesc.Count = 1;
  props.SampleDesc.Quality = 0;
  props.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  props.BufferCount = 2;
  props.Scaling = DXGI_SCALING_NONE;
  props.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; // DXGI_SWAP_EFFECT_DISCARD;
  props.Flags = 0;

  hr = dxgiFactory->CreateSwapChainForHwnd(mD3DDevice, hWnd, &props, nullptr, nullptr, &mSwapChain);

  DXGI_SWAP_CHAIN_DESC1 desc;
  hr = mSwapChain->GetDesc1(&desc);

  /*	IDXGIOutput* output;
    hr = m_swapChain->GetContainingOutput(&output);
    DXGI_MODE_DESC inputDesc = { 0 };
    DXGI_MODE_DESC matchingDesc = { 0 };
    hr = output->FindClosestMatchingMode(&inputDesc, &matchingDesc, m_d3dDevice);
    output->Release();
    */

  SafeRelease(&dxgi);
  SafeRelease(&dxadapt);
  SafeRelease(&dxgiFactory);

//  _redrawProfiler.StartProfiling();

}

void IGraphicsD2D::D2DReleaseDevice()
{
//  _redrawProfiler.StopProfiling();

  D2DReleaseSizeDependantResources();
  D2DReleaseSizeDependantResources();
  SafeRelease(&mD2DDeviceContext);
  SafeRelease(&mSwapChain);
  SafeRelease(&mSwapChainBitmap);
  SafeRelease(&mD2DDevice);
  SafeRelease(&mD3DDevice);
}

void IGraphicsD2D::D2DCreateDeviceSwapChainBitmap()
{
  IDXGISurface* surface = nullptr;
  ID2D1Bitmap1* bitmap = nullptr;

  // Get the back buffer as an IDXGISurface (Direct2D doesn't accept an ID3D11Texture2D directly as a render target)
  HRESULT hr = mSwapChain->GetBuffer(0, __uuidof(IDXGISurface), (void**)&surface);

  auto bprops = D2D1::BitmapProperties1(
    D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
    D2D1::PixelFormat(
      DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));

  hr = mD2DDeviceContext->CreateBitmapFromDxgiSurface(surface, bprops, &bitmap);
  mD2DDeviceContext->SetTarget(bitmap);

  float dpi = static_cast<float>(GetDpiForWindow(static_cast<HWND>(GetWindow())));
  mD2DDeviceContext->SetDpi(dpi, dpi);

  int w = Width();
  int h = Height();

  SafeRelease(&surface);

  // keep the bitmap for later
  //  SafeRelease(&bitmap);
  mSwapChainBitmap = bitmap;

  //@@@ setup for drawing now?
  mD2DDeviceContext->SetTarget(mSwapChainBitmap);

  //@@@
  mD2DDeviceContext->BeginDraw();
  mInDraw = true;
  mTargetSize = mD2DDeviceContext->GetSize();
  mLayerTransform = D2D1::Matrix3x2F::Scale(mTargetSize.width / Width(), mTargetSize.height / Height());
  PathTransformReset();


}

void IGraphicsD2D::D2DResizeSurface()
{
  HRESULT hr = S_OK;

  if (mD2DDeviceContext)
  {
    // get the size of the surface to make
    ::GetClientRect((HWND)GetWindow(), &swapChainRect);
    int width = swapChainRect.right - swapChainRect.left;
    int height = swapChainRect.bottom - swapChainRect.top;

    // clear the target and notify the swap chain of a new buffer

    hr = mD2DDeviceContext->EndDraw();
    mInDraw = false;
    if (hr != S_OK)
    {
      DBGMSG("Issue during completion of resize");
    }
    mD2DDeviceContext->SetTarget(nullptr);
    SafeRelease(&mSwapChainBitmap);
    HRESULT hr = mSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (hr == S_OK)
    {
      DBGMSG("resizing swap chain");
      // try to recreate the bitmap now.
      D2DCreateDeviceSwapChainBitmap();
      D2DReleaseSizeDependantResources();
    }
    else
    {
      D2DReleaseDevice();
    }
  }
}

void IGraphicsD2D::D2DReleaseDeviceDependantResources()
{
  //TODO
}

void IGraphicsD2D::D2DReleaseSizeDependantResources()
{
  //TODO

}

void IGraphicsD2D::D2DCreateFactoryResources()
{
  //TODO
}

void IGraphicsD2D::D2DReleaseFactoryResources()
{
  SafeRelease(&mPath);
  SafeRelease(&mPathSink);
  SafeRelease(&mSolidBrush);
}

ID2D1Brush* IGraphicsD2D::GetBrush(const IColor& color)
{
  if (!mSolidBrush) // TODO:: only solid color support for now
    HRESULT hr = mD2DDeviceContext->CreateSolidColorBrush(D2DColor(color), &mSolidBrush);
  else
    mSolidBrush->SetColor(D2DColor(color));

  return mSolidBrush;
}

void IGraphicsD2D::PathAddLines(float* points)
{
  //TODO
}

std::string IGraphicsD2D::FontId(const char* fontName, float fontSize)
{
  std::stringstream ss;
  ss << fontName << (int)roundf(fontSize * 1000);
  return ss.str();
}

IDWriteTextFormat* IGraphicsD2D::GetFont(const char* fontName, float fontSize)
{
  IDWriteTextFormat* font = nullptr;

  std::string fontKey = FontId(fontName, fontSize);
  auto iter = mFontCache.find(fontKey);
  if (iter != mFontCache.end())
  {
    // found it -- bump the sequence number and return the pointer
    iter->second->Sequence = mFontSequence++;
    font = iter->second->Format;
  }
  else
  {
    // create and add to map
    std::string fontName(fontName);
    HRESULT hr = mDWriteFactory->CreateTextFormat(
      s2ws(fontName).data(), NULL,
      DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
      fontSize * 1.0f,
      L"en-us", &font);
    font->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    FontCacheItem* item = new FontCacheItem(fontKey, font);
    item->Sequence = mFontSequence++;
    mFontCache[fontKey] = item;
  }

  return font;
}

void IGraphicsD2D::GarbageCollectFontCache(int maxItems)
{
  int numToRemove = std::max<int>(0, mFontCache.size() - maxItems);
  if (numToRemove == 0) return;

  // copy all items
  std::vector< FontCacheItem* > v;
  for (auto item : mFontCache)
  {
    v.push_back(item.second);
  }

  // sort sequence number, smallest to largest
  std::sort(v.begin(), v.end(), FontCacheItem::SortSeq);

  // walk until we have removed enough items;
  for (int i=0; i<numToRemove; i++)
  {
    mFontCache.erase(v[i]->Key);
    delete v[i];
  }
}

void IGraphicsD2D::NukeFontCache()
{
  for (auto item : mFontCache)
  {
    delete item.second;
  }
  mFontCache.clear();
}

HRESULT IGraphicsD2D::LoadBitmapFromFile(PCWSTR uri, ID2D1Bitmap** ppBitmap)
{
  IWICBitmapDecoder* pDecoder = NULL;
  IWICBitmapFrameDecode* pSource = NULL;
  IWICStream* pStream = NULL;
  IWICFormatConverter* pConverter = NULL;
  IWICBitmapScaler* pScaler = NULL;

  HRESULT hr = mWICFactory.Get()->CreateDecoderFromFilename(
    uri,
    NULL,
    GENERIC_READ,
    WICDecodeMetadataCacheOnLoad,
    &pDecoder
  );

  if (SUCCEEDED(hr))
  {
    // Create the initial frame.
    hr = pDecoder->GetFrame(0, &pSource);
  }

  if (SUCCEEDED(hr))
  {
    // Convert the image format to 32bppPBGRA
    // (DXGI_FORMAT_B8G8R8A8_UNORM + D2D1_ALPHA_MODE_PREMULTIPLIED).
    hr = mWICFactory.Get()->CreateFormatConverter(&pConverter);
  }

  if (SUCCEEDED(hr))
  {
    hr = pConverter->Initialize(
      pSource,
      GUID_WICPixelFormat32bppPBGRA,
      WICBitmapDitherTypeNone,
      NULL,
      0.f,
      WICBitmapPaletteTypeMedianCut
    );

    if (SUCCEEDED(hr))
    {

      // Create a Direct2D bitmap from the WIC bitmap.
      hr = mD2DDeviceContext->CreateBitmapFromWicBitmap(
        pConverter,
        NULL,
        ppBitmap
      );
    }

    SafeRelease(&pDecoder);
    SafeRelease(&pSource);
    SafeRelease(&pStream);
    SafeRelease(&pConverter);
    SafeRelease(&pScaler);

    return hr;
  }
}

HRESULT IGraphicsD2D::LoadResourceBitmap(PCWSTR resourceName, PCWSTR resourceType, ID2D1Bitmap** ppBitmap)
{
  IWICBitmapDecoder* pDecoder = NULL;
  IWICBitmapFrameDecode* pSource = NULL;
  IWICStream* pStream = NULL;
  IWICFormatConverter* pConverter = NULL;
  IWICBitmapScaler* pScaler = NULL;

  HRSRC imageResHandle = NULL;
  HGLOBAL imageResDataHandle = NULL;
  void* pImageFile = NULL;
  DWORD imageFileSize = 0;

  // Locate the resource.
  imageResHandle = FindResourceW(static_cast<HMODULE>(GetWinModuleHandle()), resourceName, resourceType);
  HRESULT hr = imageResHandle ? S_OK : E_FAIL;
  if (SUCCEEDED(hr))
  {
    // Load the resource.
    imageResDataHandle = LoadResource(static_cast<HMODULE>(GetWinModuleHandle()), imageResHandle);

    hr = imageResDataHandle ? S_OK : E_FAIL;
  }

  if (SUCCEEDED(hr))
  {
    // Lock it to get a system memory pointer.
    pImageFile = LockResource(imageResDataHandle);

    hr = pImageFile ? S_OK : E_FAIL;
  }
  if (SUCCEEDED(hr))
  {
    // Calculate the size.
    imageFileSize = SizeofResource(static_cast<HMODULE>(GetWinModuleHandle()), imageResHandle);

    hr = imageFileSize ? S_OK : E_FAIL;

  }
  if (SUCCEEDED(hr))
  {
    // Create a WIC stream to map onto the memory.
    hr = mWICFactory.Get()->CreateStream(&pStream);
  }
  if (SUCCEEDED(hr))
  {
    // Initialize the stream with the memory pointer and size.
    hr = pStream->InitializeFromMemory(
      reinterpret_cast<BYTE*>(pImageFile),
      imageFileSize
    );
  }
  if (SUCCEEDED(hr))
  {
    // Create a decoder for the stream.
    hr = mWICFactory.Get()->CreateDecoderFromStream(
      pStream,
      NULL,
      WICDecodeMetadataCacheOnLoad,
      &pDecoder
    );
  }
  if (SUCCEEDED(hr))
  {
    // Create the initial frame.
    hr = pDecoder->GetFrame(0, &pSource);
  }
  if (SUCCEEDED(hr))
  {
    // Convert the image format to 32bppPBGRA
    // (DXGI_FORMAT_B8G8R8A8_UNORM + D2D1_ALPHA_MODE_PREMULTIPLIED).
    hr = mWICFactory.Get()->CreateFormatConverter(&pConverter);
  }

  if (SUCCEEDED(hr))
  {
    hr = pConverter->Initialize(
      pSource,
      GUID_WICPixelFormat32bppPBGRA,
      WICBitmapDitherTypeNone,
      NULL,
      0.f,
      WICBitmapPaletteTypeMedianCut
    );

    if (SUCCEEDED(hr))
    {
      // force the dpi of the bitmap to match the device
      D2D1_BITMAP_PROPERTIES bp;
      bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
      bp.pixelFormat.format = DXGI_FORMAT_R8G8B8A8_UNORM;
      bp.pixelFormat = D2D1::PixelFormat();
      int dpi = GetDpiForWindow((HWND)GetWindow());
      bp.dpiX = dpi;
      bp.dpiY = dpi;

      //create a Direct2D bitmap from the WIC bitmap.
      hr = mD2DDeviceContext->CreateBitmapFromWicBitmap(
        pConverter,
        bp,
        ppBitmap
      );

    }

    SafeRelease(&pDecoder);
    SafeRelease(&pSource);
    SafeRelease(&pStream);
    SafeRelease(&pConverter);
    SafeRelease(&pScaler);

    return hr;
  }

  return 1;
}

#endif