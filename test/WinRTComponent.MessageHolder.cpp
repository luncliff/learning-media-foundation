#include "WinRTComponent.MessageHolder.h"

namespace winrt::WinRTComponent::implementation {
hstring MessageHolder::Message() {
    return m_message;
}

void MessageHolder::Message(hstring value) {
    m_message = std::move(value);
}
} // namespace winrt::WinRTComponent::implementation
