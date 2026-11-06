#pragma once

// What a plugin's own standalone app builds on: the windowless StandaloneHost
// and the default app's pieces, each optional. An app passed to
// makeasound_add_plugin as STANDALONE_SOURCES includes this; the default
// StandaloneApp itself is not part of it.

#include "../MakeASoundPlugin.h"
#include "StandaloneHost.h"
#include "SettingsPanel.h"
#include "EditorWindow.h"
#include "TypingKeyboard.h"
#include "../UI/GenericEditor.h"
