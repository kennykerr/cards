#pragma once

template <typename T>
struct Window
{
    HWND m_window = nullptr;

    static T * GetThisFromHandle(HWND const window)
    {
        return reinterpret_cast<T *>(GetWindowLongPtr(window,
                                                      GWLP_USERDATA));
    }

    static LRESULT __stdcall WndProc(HWND const window,
                                     UINT const message,
                                     WPARAM const wparam,
                                     LPARAM const lparam)
    {
        ASSERT(window);

        if (WM_NCCREATE == message)
        {
            CREATESTRUCT * cs = reinterpret_cast<CREATESTRUCT *>(lparam);
            T * that = static_cast<T *>(cs->lpCreateParams);

            ASSERT(that);
            ASSERT(!that->m_window);

            that->m_window = window;

            SetWindowLongPtr(window,
                             GWLP_USERDATA,
                             reinterpret_cast<LONG_PTR>(that));
        }
        else if (T * that = GetThisFromHandle(window))
        {
            return that->MessageHandler(message,
                                        wparam,
                                        lparam);
        }

        return DefWindowProc(window,
                             message,
                             wparam,
                             lparam);
    }

    LRESULT MessageHandler(UINT const message,
                           WPARAM const wparam,
                           LPARAM const lparam)
    {
        if (WM_DESTROY == message)
        {
            PostQuitMessage(0);
        }
        else
        {
            return DefWindowProc(m_window,
                                 message,
                                 wparam,
                                 lparam);
        }

        return 0;
    }
};
