module;

#include <shobjidl.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <wrl/client.h>
#include <string>
#include <vector>

module application;

using Microsoft::WRL::ComPtr;

std::string Application::openNativeFileDialog(
    FileDialogType type,
    const char* title,
    const std::vector<std::pair<std::string, std::string>>& filters,
    const char* defaultExtension
)
{
    std::string result;
    ComPtr<IFileDialog> pDialog;
    HRESULT hr;

    if (type == FileDialogType::Open) {
        hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_PPV_ARGS(&pDialog));
    } else {
        hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_ALL, IID_PPV_ARGS(&pDialog));
    }

    if (FAILED(hr)) {
        spdlog::error("Failed to create FileDialog instance");
        return "";
    }

    // Set title
    if (title) {
        std::wstring wTitle(title, title + strlen(title));
        pDialog->SetTitle(wTitle.c_str());
    }

    // Set filters
    if (!filters.empty()) {
        std::vector<COMDLG_FILTERSPEC> specs;
        std::vector<std::wstring> wNames;
        std::vector<std::wstring> wSpecs;
        specs.reserve(filters.size());
        wNames.reserve(filters.size());
        wSpecs.reserve(filters.size());
        for (const auto& f : filters) {
            wNames.push_back(std::wstring(f.first.begin(), f.first.end()));
            wSpecs.push_back(std::wstring(f.second.begin(), f.second.end()));
            specs.push_back({ wNames.back().c_str(), wSpecs.back().c_str() });
        }
        pDialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    }

    // Set default extension
    if (defaultExtension) {
        std::wstring wExt(defaultExtension, defaultExtension + strlen(defaultExtension));
        pDialog->SetDefaultExtension(wExt.c_str());
    }

    // Show dialog
    hr = pDialog->Show(hWnd);
    if (SUCCEEDED(hr)) {
        ComPtr<IShellItem> pItem;
        hr = pDialog->GetResult(&pItem);
        if (SUCCEEDED(hr)) {
            PWSTR pszFilePath;
            hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
            if (SUCCEEDED(hr)) {
                int size_needed =
                    WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, NULL, 0, NULL, NULL);
                if (size_needed > 0) {
                    result.assign(size_needed, '\0');
                    WideCharToMultiByte(
                        CP_UTF8, 0, pszFilePath, -1, result.data(), size_needed, NULL, NULL
                    );
                    result.resize(size_needed - 1);
                }
                CoTaskMemFree(pszFilePath);
            }
        }
    }

    return result;
}
