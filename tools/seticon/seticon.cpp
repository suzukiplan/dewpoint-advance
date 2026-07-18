#include <windows.h>
#include <wincodec.h>

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

namespace
{

constexpr UINT kIconSizes[] = {16, 24, 32, 48, 64, 128, 256};

#pragma pack(push, 1)
struct GroupIconHeader {
    std::uint16_t reserved;
    std::uint16_t type;
    std::uint16_t count;
};

struct GroupIconEntry {
    std::uint8_t width;
    std::uint8_t height;
    std::uint8_t colorCount;
    std::uint8_t reserved;
    std::uint16_t planes;
    std::uint16_t bitCount;
    std::uint32_t bytesInResource;
    std::uint16_t resourceId;
};
#pragma pack(pop)

template <typename T>
class ComPtr
{
public:
    ~ComPtr()
    {
        if (value_) {
            value_->Release();
        }
    }

    T* get() const { return value_; }
    T** put() { return &value_; }
    T* operator->() const { return value_; }

private:
    T* value_ = nullptr;
};

class ComInitializer
{
public:
    ~ComInitializer() { CoUninitialize(); }
};

void reportHresult(const wchar_t* operation, HRESULT result)
{
    std::wcerr << L"seticon: " << operation << L" failed (HRESULT 0x" << std::hex
               << std::setw(8) << std::setfill(L'0') << static_cast<unsigned long>(result)
               << L")\n";
}

void reportWin32(const wchar_t* operation)
{
    std::wcerr << L"seticon: " << operation << L" failed (Win32 error " << GetLastError()
               << L")\n";
}

bool encodePng(IWICImagingFactory* factory, IWICBitmapSource* source, UINT size,
               std::vector<std::uint8_t>& output)
{
    ComPtr<IWICBitmapScaler> scaler;
    HRESULT result = factory->CreateBitmapScaler(scaler.put());
    if (FAILED(result)) {
        reportHresult(L"creating bitmap scaler", result);
        return false;
    }
    result = scaler->Initialize(source, size, size, WICBitmapInterpolationModeFant);
    if (FAILED(result)) {
        reportHresult(L"scaling icon", result);
        return false;
    }

    ComPtr<IStream> stream;
    result = CreateStreamOnHGlobal(nullptr, TRUE, stream.put());
    if (FAILED(result)) {
        reportHresult(L"creating memory stream", result);
        return false;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put());
    if (FAILED(result)) {
        reportHresult(L"creating PNG encoder", result);
        return false;
    }
    result = encoder->Initialize(stream.get(), WICBitmapEncoderNoCache);
    if (FAILED(result)) {
        reportHresult(L"initializing PNG encoder", result);
        return false;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    result = encoder->CreateNewFrame(frame.put(), nullptr);
    if (SUCCEEDED(result)) {
        result = frame->Initialize(nullptr);
    }
    if (SUCCEEDED(result)) {
        result = frame->SetSize(size, size);
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(result)) {
        result = frame->SetPixelFormat(&format);
    }
    if (SUCCEEDED(result)) {
        result = frame->WriteSource(scaler.get(), nullptr);
    }
    if (SUCCEEDED(result)) {
        result = frame->Commit();
    }
    if (SUCCEEDED(result)) {
        result = encoder->Commit();
    }
    if (FAILED(result)) {
        reportHresult(L"encoding PNG icon", result);
        return false;
    }

    HGLOBAL memory = nullptr;
    result = GetHGlobalFromStream(stream.get(), &memory);
    if (FAILED(result)) {
        reportHresult(L"reading encoded PNG", result);
        return false;
    }
    STATSTG statistics{};
    result = stream->Stat(&statistics, STATFLAG_NONAME);
    if (FAILED(result) || statistics.cbSize.HighPart != 0) {
        reportHresult(L"measuring encoded PNG", FAILED(result) ? result : E_OUTOFMEMORY);
        return false;
    }
    const SIZE_T length = statistics.cbSize.LowPart;
    const void* bytes = GlobalLock(memory);
    if (!bytes || length == 0 || length > GlobalSize(memory)) {
        reportWin32(L"locking encoded PNG");
        return false;
    }
    output.assign(static_cast<const std::uint8_t*>(bytes),
                  static_cast<const std::uint8_t*>(bytes) + length);
    GlobalUnlock(memory);
    return true;
}

template <typename T>
void append(std::vector<std::uint8_t>& output, const T& value)
{
    const auto* first = reinterpret_cast<const std::uint8_t*>(&value);
    output.insert(output.end(), first, first + sizeof(value));
}

bool updateExecutable(const wchar_t* executablePath,
                      const std::vector<std::vector<std::uint8_t>>& images)
{
    HANDLE update = BeginUpdateResourceW(executablePath, FALSE);
    if (!update) {
        reportWin32(L"opening executable resources");
        return false;
    }

    bool success = true;
    std::vector<std::uint8_t> group;
    append(group, GroupIconHeader{0, 1, static_cast<std::uint16_t>(images.size())});
    for (std::size_t index = 0; index < images.size(); ++index) {
        const WORD resourceId = static_cast<WORD>(index + 1);
        const UINT size = kIconSizes[index];
        const auto& image = images[index];
        if (!UpdateResourceW(update, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(resourceId),
                             MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                             const_cast<std::uint8_t*>(image.data()),
                             static_cast<DWORD>(image.size()))) {
            reportWin32(L"writing icon resource");
            success = false;
            break;
        }
        append(group, GroupIconEntry{
                          static_cast<std::uint8_t>(size == 256 ? 0 : size),
                          static_cast<std::uint8_t>(size == 256 ? 0 : size),
                          0,
                          0,
                          1,
                          32,
                          static_cast<std::uint32_t>(image.size()),
                          resourceId,
                      });
    }

    if (success &&
        !UpdateResourceW(update, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(1),
                         MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), group.data(),
                         static_cast<DWORD>(group.size()))) {
        reportWin32(L"writing icon group resource");
        success = false;
    }

    if (!EndUpdateResourceW(update, success ? FALSE : TRUE)) {
        reportWin32(success ? L"committing executable resources" : L"discarding executable resources");
        return false;
    }
    return success;
}

} // namespace

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 3) {
        std::wcerr << L"usage: seticon /path/to/icon.png /path/to/application.exe\n";
        return 1;
    }

    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) {
        reportHresult(L"initializing COM", initialized);
        return 1;
    }
    ComInitializer comInitializer;

    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(factory.put()));
    if (FAILED(result)) {
        reportHresult(L"creating Windows Imaging Component factory", result);
        return 1;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromFilename(argv[1], nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, decoder.put());
    if (FAILED(result)) {
        reportHresult(L"opening icon PNG", result);
        return 1;
    }

    ComPtr<IWICBitmapFrameDecode> source;
    result = decoder->GetFrame(0, source.put());
    if (FAILED(result)) {
        reportHresult(L"decoding icon PNG", result);
        return 1;
    }

    std::vector<std::vector<std::uint8_t>> images;
    images.reserve(sizeof(kIconSizes) / sizeof(kIconSizes[0]));
    bool success = true;
    for (const UINT size : kIconSizes) {
        images.emplace_back();
        if (!encodePng(factory.get(), source.get(), size, images.back())) {
            success = false;
            break;
        }
    }
    if (success) {
        success = updateExecutable(argv[2], images);
    }

    return success ? 0 : 1;
}
