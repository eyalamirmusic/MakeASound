// The app extension's executable, compiled into each <Name>-AUv3: Foundation's
// NSExtensionMain, which no header declares, runs the extension and instantiates
// the principal class its Info.plist names. Everything else is in the objects and
// archives the executable links.
extern "C" int NSExtensionMain(int argc, char* argv[]);

int main(int argc, char* argv[])
{
    return NSExtensionMain(argc, argv);
}
