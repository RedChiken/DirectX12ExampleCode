#include <comdef.h>
#include "DXException.h"

DXException::DXException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber)
    : ErrorCode(hr), FunctionName(functionName), FileName(filename), LineNumber(lineNumber)
{

}

std::wstring DXException::ToString() const
{
    _com_error err(ErrorCode);
    std::wstring msg = err.ErrorMessage();

    return FunctionName + L" failed in " + FileName + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}
