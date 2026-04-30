#include "AudioProcessor.h"
#include "SynthUI.h"

struct SynthApp
{
    AudioProcessor processor;
    SynthUI ui {processor};
};

int main()
{
    eacp::Apps::run<SynthApp>();
    return 0;
}
