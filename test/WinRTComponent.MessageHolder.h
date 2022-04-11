#pragma once
#include "WinRTComponent/MessageHolder.g.h"

namespace winrt::WinRTComponent::implementation {
struct MessageHolder : MessageHolderT<MessageHolder> {
  private:
    hstring m_message;

  public:
    MessageHolder() = default;

    hstring Message();
    void Message(hstring value);
};
} // namespace winrt::WinRTComponent::implementation
namespace winrt::WinRTComponent::factory_implementation {
struct MessageHolder : MessageHolderT<MessageHolder, implementation::MessageHolder> {};
} // namespace winrt::WinRTComponent::factory_implementation
