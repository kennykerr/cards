#include "Precompiled.h"
#include "window.h"

using namespace Microsoft::WRL;
using namespace D2D1;
using namespace std;

extern "C" IMAGE_DOS_HEADER __ImageBase;

static unsigned const CardRows = 3;
static unsigned const CardColumns = 6;
static float const CardMargin = 15.0f;
static float const CardWidth = 150.0f;
static float const CardHeight = 210.0f;

static float const WindowWidth =
    CardColumns * (CardWidth + CardMargin) + CardMargin;

static float const WindowHeight =
    CardRows * (CardHeight + CardMargin) + CardMargin;

struct ComException
{
    HRESULT result;

    ComException(HRESULT const value) :
        result(value)
    {}
};

static void HR(HRESULT const result)
{
    if (S_OK != result)
    {
        throw ComException(result);
    }
}

template <typename T>
static float PhysicalToLogical(T const pixel,
                               float const dpi)
{
    return pixel * 96.0f / dpi;
}

template <typename T>
static float LogicalToPhysical(T const pixel,
                               float const dpi)
{
    return pixel * dpi / 96.0f;
}

enum class CardStatus
{
    Hidden,
    Selected,
    Matched
};

struct Card
{
    // Device independent resources
    CardStatus Status = CardStatus::Hidden;
    wchar_t Value = L' ';
    float OffsetX = 0.0f;
    float OffsetY = 0.0f;
    ComPtr<IUIAnimationVariable2> Variable;

    // Device resources
    ComPtr<IDCompositionRotateTransform3D> Rotation;
};

struct SampleWindow : Window<SampleWindow>
{
    // Device independent resources
    float m_dpiX = 0.0f;
    float m_dpiY = 0.0f;
    ComPtr<IDWriteTextFormat> m_textFormat;
    ComPtr<IWICFormatConverter> m_image;
    ComPtr<IUIAnimationManager2> m_manager;
    ComPtr<IUIAnimationTransitionLibrary2> m_library;
    Card * m_firstCard = nullptr;

    // Contains some device resources
    array<Card, CardRows * CardColumns> m_cards;

    // Device resources
    ComPtr<ID3D11Device> m_device3D;
    ComPtr<IDCompositionDesktopDevice> m_device;
    ComPtr<IDCompositionTarget> m_target;

    SampleWindow()
    {
        CreateDesktopWindow();
        ShuffleCards();
        CreateTextFormat();
        CreateImage();
        PrepareAnimationManager();
    }

    void PrepareAnimationManager()
    {
        HR(CoCreateInstance(__uuidof(UIAnimationManager2),
                            nullptr,
                            CLSCTX_INPROC,
                            __uuidof(m_manager),
                            reinterpret_cast<void **>(m_manager.GetAddressOf())));

        HR(CoCreateInstance(__uuidof(UIAnimationTransitionLibrary2),
                            nullptr,
                            CLSCTX_INPROC,
                            __uuidof(m_library),
                            reinterpret_cast<void **>(m_library.GetAddressOf())));

        for (Card & card : m_cards)
        {
            HR(m_manager->CreateAnimationVariable(0.0, card.Variable.GetAddressOf()));
        }
    }

    void CreateImage()
    {
        ComPtr<IWICImagingFactory2> factory;

        HR(CoCreateInstance(CLSID_WICImagingFactory,
                            nullptr,
                            CLSCTX_INPROC,
                            __uuidof(factory),
                            reinterpret_cast<void **>(factory.GetAddressOf())));

        ComPtr<IWICBitmapDecoder> decoder;

        HR(factory->CreateDecoderFromFilename(L"background.jpg",
                                              nullptr,
                                              GENERIC_READ,
                                              WICDecodeMetadataCacheOnDemand,
                                              decoder.GetAddressOf()));

        ComPtr<IWICBitmapFrameDecode> source;

        HR(decoder->GetFrame(0, source.GetAddressOf()));

        HR(factory->CreateFormatConverter(m_image.GetAddressOf()));

        HR(m_image->Initialize(source.Get(),
                               GUID_WICPixelFormat32bppBGR,
                               WICBitmapDitherTypeNone,
                               nullptr,
                               0.0,
                               WICBitmapPaletteTypeMedianCut));
    }

    void CreateTextFormat()
    {
        ComPtr<IDWriteFactory2> factory;

        HR(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(factory),
            reinterpret_cast<IUnknown **>(factory.GetAddressOf())));

        HR(factory->CreateTextFormat(L"Candara",
                                     nullptr,
                                     DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL,
                                     CardHeight / 2.0f,
                                     L"en",
                                     m_textFormat.GetAddressOf()));

        HR(m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
        HR(m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
    }

    void ShuffleCards()
    {
        random_device device;
        mt19937 generator(device());
        uniform_int_distribution<short> const distribution(L'A', L'Z');

        array<wchar_t, CardRows * CardColumns> values;

        for (unsigned i = 0; i != CardRows * CardColumns / 2; ++i)
        {
            wchar_t const value = distribution(generator);

            values[i * 2 + 0] = value;
            values[i * 2 + 1] = towlower(value);
        }

        shuffle(begin(values), end(values), generator);

        for (unsigned i = 0; i != CardRows * CardColumns; ++i)
        {
            Card & card = m_cards[i];
            card.Value = values[i];
            card.Status = CardStatus::Hidden;
        }

        #ifdef _DEBUG

        for (unsigned row = 0; row != CardRows; ++row)
        {
            for (unsigned column = 0; column != CardColumns; ++column)
            {
                Card & card = m_cards[row * CardColumns + column];
                TRACE(L"%c ", card.Value);
            }

            TRACE(L"\n");
        }

        TRACE(L"\n");

        #endif
    }

    void CreateDesktopWindow()
    {
        WNDCLASS wc = {};
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hInstance = reinterpret_cast<HINSTANCE>(&__ImageBase);
        wc.lpszClassName = L"SampleWindow";
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;

        RegisterClass(&wc);

        ASSERT(!m_window);

        VERIFY(CreateWindowEx(WS_EX_NOREDIRECTIONBITMAP,
                              wc.lpszClassName,
                              L"Sample Window",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              nullptr,
                              nullptr,
                              wc.hInstance,
                              this));

        ASSERT(m_window);
    }

    bool IsDeviceCreated() const
    {
        return m_device3D;
    }

    void ReleaseDeviceResources()
    {
        m_device3D.Reset();
    }

    void CreateDevice3D()
    {
        ASSERT(!IsDeviceCreated());

        unsigned flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT |
                         D3D11_CREATE_DEVICE_SINGLETHREADED;

        #ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
        #endif

        HR(D3D11CreateDevice(nullptr,
                             D3D_DRIVER_TYPE_HARDWARE,
                             nullptr,
                             flags,
                             nullptr, 0,
                             D3D11_SDK_VERSION,
                             m_device3D.GetAddressOf(),
                             nullptr,
                             nullptr));
    }

    ComPtr<ID2D1Device> CreateDevice2D()
    {
        ComPtr<IDXGIDevice3> deviceX;
        HR(m_device3D.As(&deviceX));

        D2D1_CREATION_PROPERTIES properties = {};

        #ifdef _DEBUG
        properties.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
        #endif

        ComPtr<ID2D1Device> device2D;

        HR(D2D1CreateDevice(deviceX.Get(),
                            properties,
                            device2D.GetAddressOf()));

        return device2D;
    }

    ComPtr<IDCompositionVisual2> CreateVisual()
    {
        ComPtr<IDCompositionVisual2> visual;

        HR(m_device->CreateVisual(visual.GetAddressOf()));

        HR(visual->SetBackFaceVisibility(DCOMPOSITION_BACKFACE_VISIBILITY_HIDDEN));

        return visual;
    }

    template <typename T>
    ComPtr<IDCompositionSurface> CreateSurface(T const width,
                                               T const height)
    {
        ComPtr<IDCompositionSurface> surface;

        HR(m_device->CreateSurface(static_cast<unsigned>(width),
                                   static_cast<unsigned>(height),
                                   DXGI_FORMAT_B8G8R8A8_UNORM,
                                   DXGI_ALPHA_MODE_PREMULTIPLIED,
                                   surface.GetAddressOf()));

        return surface;
    }

    void CreateDeviceResources()
    {
        ASSERT(!IsDeviceCreated());

        CreateDevice3D();

        ComPtr<ID2D1Device> const device2D = CreateDevice2D();

        HR(DCompositionCreateDevice2(
            device2D.Get(),
            __uuidof(m_device),
            reinterpret_cast<void **>(m_device.ReleaseAndGetAddressOf())));

        HR(m_device->CreateTargetForHwnd(m_window,
                                         true,
                                         m_target.ReleaseAndGetAddressOf()));

        ComPtr<IDCompositionVisual2> rootVisual = CreateVisual();

        HR(m_target->SetRoot(rootVisual.Get()));

        ComPtr<ID2D1DeviceContext> dc;

        HR(device2D->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                         dc.GetAddressOf()));

        D2D1_COLOR_F const color = ColorF(0.0f, 0.0f, 0.0f);

        ComPtr<ID2D1SolidColorBrush> brush;

        HR(dc->CreateSolidColorBrush(color,
                                     brush.GetAddressOf()));

        ComPtr<ID2D1Bitmap1> bitmap;

        HR(dc->CreateBitmapFromWicBitmap(m_image.Get(),
                                         bitmap.GetAddressOf()));

        float const width = LogicalToPhysical(CardWidth, m_dpiX);
        float const height = LogicalToPhysical(CardHeight, m_dpiY);

        for (unsigned row = 0; row != CardRows; ++row)
        for (unsigned column = 0; column != CardColumns; ++column)
        {
            Card & card = m_cards[row * CardColumns + column];

            card.OffsetX = LogicalToPhysical(column * (CardWidth + CardMargin) + CardMargin, m_dpiX);
            card.OffsetY = LogicalToPhysical(row * (CardHeight + CardMargin) + CardMargin, m_dpiY);

            if (card.Status == CardStatus::Matched) continue;

            ComPtr<IDCompositionVisual2> frontVisual = CreateVisual();
            HR(frontVisual->SetOffsetX(card.OffsetX));
            HR(frontVisual->SetOffsetY(card.OffsetY));

            HR(rootVisual->AddVisual(frontVisual.Get(), false, nullptr));

            ComPtr<IDCompositionVisual2> backVisual = CreateVisual();
            HR(backVisual->SetOffsetX(card.OffsetX));
            HR(backVisual->SetOffsetY(card.OffsetY));

            HR(rootVisual->AddVisual(backVisual.Get(), false, nullptr));

            ComPtr<IDCompositionSurface> frontSurface =
                CreateSurface(width, height);

            HR(frontVisual->SetContent(frontSurface.Get()));

            DrawCardFront(frontSurface,
                          card.Value,
                          brush);

            ComPtr<IDCompositionSurface> backSurface =
                CreateSurface(width, height);

            HR(backVisual->SetContent(backSurface.Get()));

            DrawCardBack(backSurface,
                         card.OffsetX,
                         card.OffsetY,
                         bitmap);

            HR(m_device->CreateRotateTransform3D(card.Rotation.ReleaseAndGetAddressOf()));

            if (card.Status == CardStatus::Selected)
            {
                HR(card.Rotation->SetAngle(180.0f));
            }

            HR(card.Rotation->SetAxisZ(0.0f));
            HR(card.Rotation->SetAxisY(1.0f));

            CreateEffect(frontVisual,
                         card.Rotation,
                         true);

            CreateEffect(backVisual,
                         card.Rotation,
                         false);
        }

        HR(m_device->Commit());
    }

    void CreateEffect(ComPtr<IDCompositionVisual2> const & visual,
                      ComPtr<IDCompositionRotateTransform3D> const & rotation,
                      bool const front)
    {
        float const width = LogicalToPhysical(CardWidth, m_dpiX);
        float const height = LogicalToPhysical(CardHeight, m_dpiY);

        ComPtr<IDCompositionMatrixTransform3D> pre;
        HR(m_device->CreateMatrixTransform3D(pre.GetAddressOf()));

        D2D1_MATRIX_4X4_F preMatrix =
            Matrix4x4F::Translation(-width / 2.0f, -height / 2.0f, 0.0f) *
            Matrix4x4F::RotationY(front ? 180.0f : 0.0f);

        HR(pre->SetMatrix(reinterpret_cast<D3DMATRIX const &>(preMatrix)));

        ComPtr<IDCompositionMatrixTransform3D> post;
        HR(m_device->CreateMatrixTransform3D(post.GetAddressOf()));

        D2D1_MATRIX_4X4_F postMatrix =
            Matrix4x4F::PerspectiveProjection(width * 2.0f) *
            Matrix4x4F::Translation(width / 2.0f, height / 2.0f, 0.0f);

        HR(post->SetMatrix(reinterpret_cast<D3DMATRIX const &>(postMatrix)));

        IDCompositionTransform3D * transforms[] =
        {
            pre.Get(),
            rotation.Get(),
            post.Get()
        };

        ComPtr<IDCompositionTransform3D> transform;

        HR(m_device->CreateTransform3DGroup(transforms,
                                            _countof(transforms),
                                            transform.GetAddressOf()));

        HR(visual->SetEffect(transform.Get()));
    }

    void DrawCardBack(ComPtr<IDCompositionSurface> const & surface,
                      float const offsetX,
                      float const offsetY,
                      ComPtr<ID2D1Bitmap1> const & bitmap)
    {
        ComPtr<ID2D1DeviceContext> dc;
        POINT offset = {};

        HR(surface->BeginDraw(nullptr,
                              __uuidof(dc),
                              reinterpret_cast<void **>(dc.GetAddressOf()),
                              &offset));

        dc->SetDpi(m_dpiX,
                   m_dpiY);

        dc->SetTransform(Matrix3x2F::Translation(PhysicalToLogical(offset.x, m_dpiX),
                                                 PhysicalToLogical(offset.y, m_dpiY)));

        D2D1_RECT_F source = RectF(PhysicalToLogical(offsetX, m_dpiX),
                                   PhysicalToLogical(offsetY, m_dpiY));

        source.right = source.left + CardWidth;
        source.bottom = source.top + CardHeight;

        dc->DrawBitmap(bitmap.Get(),
                       nullptr,
                       1.0f,
                       D2D1_INTERPOLATION_MODE_LINEAR,
                       &source);

        HR(surface->EndDraw());
    }

    void DrawCardFront(ComPtr<IDCompositionSurface> const & surface,
                       wchar_t const value,
                       ComPtr<ID2D1SolidColorBrush> const & brush)
    {
        ComPtr<ID2D1DeviceContext> dc;
        POINT offset = {};

        HR(surface->BeginDraw(nullptr,
                              __uuidof(dc),
                              reinterpret_cast<void **>(dc.GetAddressOf()),
                              &offset));

        dc->SetDpi(m_dpiX,
                   m_dpiY);

        dc->SetTransform(Matrix3x2F::Translation(PhysicalToLogical(offset.x, m_dpiX),
                                                 PhysicalToLogical(offset.y, m_dpiY)));

        dc->Clear(ColorF(1.0f, 1.0f, 1.0f));

        dc->DrawText(&value,
                     1, 
                     m_textFormat.Get(),
                     RectF(0.0f, 0.0f, CardWidth, CardHeight),
                     brush.Get());

        HR(surface->EndDraw());
    }

    LRESULT MessageHandler(UINT const message,
                           WPARAM const wparam,
                           LPARAM const lparam)
    {
        if (WM_LBUTTONUP == message)
        {
            LeftButtonUpHandler(lparam);
        }
        else if (WM_PAINT == message)
        {
            PaintHandler();
        }
        else if (WM_DPICHANGED == message)
        {
            DpiChangedHandler(wparam, lparam);
        }
        else if (WM_CREATE == message)
        {
            CreateHandler();
        }
        else if (WM_WINDOWPOSCHANGING == message)
        {
            // Prevent window resizing due to device loss
        }
        else
        {
            return __super::MessageHandler(message,
                                           wparam,
                                           lparam);
        }

        return 0;
    }

    Card * CardAtPoint(LPARAM const lparam)
    {
        float const x = static_cast<float>(LOWORD(lparam));
        float const y = static_cast<float>(HIWORD(lparam));

        float const width = LogicalToPhysical(CardWidth, m_dpiX);
        float const height = LogicalToPhysical(CardHeight, m_dpiY);

        for (Card & card : m_cards)
        {
            if (x > card.OffsetX && 
                y > card.OffsetY &&
                x < card.OffsetX + width &&
                y < card.OffsetY + height)
            {
                return &card;
            }
        }

        return nullptr;
    }

    bool IsMatch(wchar_t const first,
                 wchar_t const second)
    {
        int const expected = 'a' - 'A';

        int const actual = abs(first - second);

        return expected == actual;
    }

    ComPtr<IUIAnimationTransition2> CreateTransition(double const duration,
                                                     double const finalValue)
    {
        ComPtr<IUIAnimationTransition2> transition;

        HR(m_library->CreateAccelerateDecelerateTransition(duration,
                                                           finalValue,
                                                           0.2,
                                                           0.8,
                                                           transition.GetAddressOf()));

        return transition;
    }

    UI_ANIMATION_KEYFRAME AddShowTransition(Card const & card,
                                            ComPtr<IUIAnimationStoryboard2> const & storyboard)
    {
        double angle = 0.0;
        HR(card.Variable->GetValue(&angle));

        double const duration = (180.0 - angle) / 180.0;

        ComPtr<IUIAnimationTransition2> transition =
            CreateTransition(duration, 180.0);

        HR(storyboard->AddTransition(card.Variable.Get(),
                                     transition.Get()));

        UI_ANIMATION_KEYFRAME keyframe = nullptr;

        HR(storyboard->AddKeyframeAfterTransition(transition.Get(),
                                                  &keyframe));

        return keyframe;
    }

    void AddHideTransition(Card const & card,
                           ComPtr<IUIAnimationStoryboard2> const & storyboard,
                           UI_ANIMATION_KEYFRAME keyframe,
                           double const finalValue)
    {
        ComPtr<IUIAnimationTransition2> transition =
            CreateTransition(1.0, finalValue);

        HR(storyboard->AddTransitionAtKeyframe(card.Variable.Get(),
                                               transition.Get(),
                                               keyframe));
    }

    void UpdateAnimation(Card const & card)
    {
        ComPtr<IDCompositionAnimation> animation;
        HR(m_device->CreateAnimation(animation.GetAddressOf()));
        HR(card.Variable->GetCurve(animation.Get()));
        HR(card.Rotation->SetAngle(animation.Get()));
    }

    void LeftButtonUpHandler(LPARAM const lparam)
    {
        try
        {
            Card * nextCard = CardAtPoint(lparam);

            if (!nextCard) return;

            if (nextCard == m_firstCard) return;

            if (nextCard->Status == CardStatus::Matched) return;

            DCOMPOSITION_FRAME_STATISTICS stats = {};
            HR(m_device->GetFrameStatistics(&stats));

            double const next = static_cast<double>(stats.nextEstimatedFrameTime.QuadPart) / stats.timeFrequency.QuadPart;

            HR(m_manager->Update(next));

            ComPtr<IUIAnimationStoryboard2> storyboard;
            HR(m_manager->CreateStoryboard(storyboard.GetAddressOf()));

            if (!m_firstCard)
            {
                m_firstCard = nextCard;
                nextCard->Status = CardStatus::Selected;

                AddShowTransition(*nextCard, storyboard);
                HR(storyboard->Schedule(next));
                UpdateAnimation(*nextCard);
            }
            else
            {
                m_firstCard->Status = CardStatus::Hidden;

                if (IsMatch(m_firstCard->Value, nextCard->Value))
                {
                    m_firstCard->Status = nextCard->Status = CardStatus::Matched;

                    UI_ANIMATION_KEYFRAME keyframe =
                        AddShowTransition(*nextCard, storyboard);

                    AddHideTransition(*m_firstCard,
                                      storyboard,
                                      keyframe,
                                      90.0);

                    AddHideTransition(*nextCard,
                                      storyboard,
                                      keyframe,
                                      90.0);
                }
                else
                {
                    UI_ANIMATION_KEYFRAME keyframe =
                        AddShowTransition(*nextCard, storyboard);

                    AddHideTransition(*m_firstCard,
                                      storyboard,
                                      keyframe,
                                      0.0);

                    AddHideTransition(*nextCard,
                                      storyboard,
                                      keyframe,
                                      0.0);
                }

                HR(storyboard->Schedule(next));
                UpdateAnimation(*m_firstCard);
                UpdateAnimation(*nextCard);

                m_firstCard = nullptr;
            }

            HR(m_device->Commit());
        }
        catch (ComException const & e)
        {
            TRACE(L"LeftButtonUpHandler failed 0x%X\n", e.result);

            ReleaseDeviceResources();

            VERIFY(InvalidateRect(m_window,
                                  nullptr,
                                  false));
        }
    }

    void DpiChangedHandler(WPARAM const wparam,
                           LPARAM const lparam)
    {
        m_dpiX = LOWORD(wparam);
        m_dpiY = HIWORD(wparam);

        RECT const * suggested =
            reinterpret_cast<RECT const *>(lparam);

        D2D1_SIZE_U const size = GetEffectiveWindowSize();

        VERIFY(SetWindowPos(m_window,
                            nullptr,
                            suggested->left,
                            suggested->top,
                            size.width,
                            size.height,
                            SWP_NOACTIVATE | SWP_NOZORDER));

        ReleaseDeviceResources();
    }

    D2D1_SIZE_U GetEffectiveWindowSize()
    {
        RECT rect =
        {
            0,
            0,
            static_cast<int>(LogicalToPhysical(WindowWidth, m_dpiX)),
            static_cast<int>(LogicalToPhysical(WindowHeight, m_dpiY))
        };

        VERIFY(AdjustWindowRect(&rect,
                                GetWindowLong(m_window, GWL_STYLE),
                                false));

        return SizeU(rect.right - rect.left,
                     rect.bottom - rect.top);
    }

    void CreateHandler()
    {
        HMONITOR const monitor = MonitorFromWindow(m_window,
                                                   MONITOR_DEFAULTTONEAREST);

        unsigned dpiX = 0;
        unsigned dpiY = 0;

        HR(GetDpiForMonitor(monitor,
                            MDT_EFFECTIVE_DPI,
                            &dpiX,
                            &dpiY));

        m_dpiX = static_cast<float>(dpiX);
        m_dpiY = static_cast<float>(dpiY);

        D2D1_SIZE_U const size = GetEffectiveWindowSize();

        VERIFY(SetWindowPos(m_window,
                            nullptr,
                            0, 0,
                            size.width,
                            size.height,
                            SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER));
    }

    void PaintHandler()
    {
        try
        {
            RECT rect = {};
            VERIFY(GetClientRect(m_window, &rect));

            if (IsDeviceCreated())
            {
                HR(m_device3D->GetDeviceRemovedReason());
            }
            else
            {
                CreateDeviceResources();
            }

            VERIFY(ValidateRect(m_window, nullptr));
        }
        catch (ComException const & e)
        {
            TRACE(L"PaintHandler failed 0x%X\n", e.result);

            ReleaseDeviceResources();
        }
    }
};

int __stdcall wWinMain(HINSTANCE, 
                       HINSTANCE, 
                       PWSTR, 
                       int)
{
    HR(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    SampleWindow window;
    MSG message;

    while (GetMessage(&message, nullptr, 0, 0))
    {
        DispatchMessage(&message);
    }
}
