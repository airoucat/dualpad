#pragma once

namespace dualpad::input
{
    class PollStateCommitter
    {
    public:
        static PollStateCommitter& GetSingleton();

        bool Commit(void* currentStateBlock);

    private:
        PollStateCommitter() = default;
    };
}
