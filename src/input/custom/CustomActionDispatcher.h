#pragma once
#include <string_view>

namespace dualpad::input::custom
{
    class CustomActionDispatcher
    {
    public:
        static CustomActionDispatcher& GetSingleton();

        void Start();
        void Stop();

        bool Execute(std::string_view actionId);

    private:
        CustomActionDispatcher() = default;

        bool ExecuteScreenshotAction();
    };
}
