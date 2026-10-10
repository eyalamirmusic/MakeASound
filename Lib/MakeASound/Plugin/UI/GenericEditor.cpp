#include "GenericEditor.h"
#include "../BoolParam.h"
#include "../ChoiceParam.h"

#include <eacp/Core/Core.h>
#include <eacp/UI/UI.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace MakeASound
{
namespace Widgets = eacp::UI;

namespace
{
constexpr auto padding = 12.f;
constexpr auto rowHeight = 32.f;
constexpr auto controlHeight = 24.f;
constexpr auto nameWidth = 130.f;
constexpr auto valueWidth = 90.f;
constexpr auto refreshHz = 30;
constexpr auto pageWidth = 420;
constexpr auto minPageHeight = 96;
constexpr auto maxPageHeight = 720;

// One parameter: its name, a control picked by type, and its value text.
class Row final : public Widgets::Component
{
public:
    Row(Plugin& pluginToUse, int indexToUse)
        : plugin(pluginToUse)
        , index(indexToUse)
        , param(plugin.parameters()[index])
        , name(plugin.parameters().entry(index).displayName)
    {
        value.setJustification(Widgets::Justification::Right);
        value.setColour(Widgets::defaultTheme().dimText);

        if (auto* choice = dynamic_cast<ChoiceParam*>(&param))
            makeCombo(*choice);
        else if (dynamic_cast<BoolParam*>(&param) != nullptr)
            makeCheckbox();
        else
            makeSlider();

        addAndMakeVisible(name);
        addAndMakeVisible(*control);
        addAndMakeVisible(value);

        pull();
    }

    // Shows the parameter's value, touching a widget only when it moved.
    void pull()
    {
        if (dragging)
            return;

        auto plain = param.getValue();

        if (plain == shown)
            return;

        shown = plain;

        if (slider != nullptr)
            slider->setValue(param.getNormalized());
        else if (combo != nullptr)
            combo->setSelectedIndex(static_cast<int>(std::lround(plain)));
        else if (checkbox != nullptr)
            checkbox->setChecked(plain >= 0.5f);

        value.setText(param.valueToText(plain));
    }

    void resized() override
    {
        auto area = getLocalBounds().inset(padding, 0.f);
        auto inset = (area.h - controlHeight) * 0.5f;

        name.setBounds(area.removeFromLeft(nameWidth));
        value.setBounds(area.removeFromRight(valueWidth));
        area.removeFromRight(8.f);
        control->setBounds(area.inset(0.f, inset));
    }

private:
    void makeSlider()
    {
        slider = EA::makeOwned<Widgets::Slider>();
        slider->setDefaultValue(param.toNormalized(param.defaultValue()));

        slider->onDragStart = [this]
        {
            dragging = true;
            begin();
        };

        slider->onValueChange = [this](float normalized)
        {
            param.setNormalized(normalized);

            // The parameter snaps a stepped value; the thumb follows it.
            slider->setValue(param.getNormalized());
            value.setText(param.valueToText(param.getValue()));
            perform();
        };

        slider->onDragEnd = [this]
        {
            dragging = false;
            end();
            shown = std::numeric_limits<float>::quiet_NaN();
        };

        control = slider.get();
    }

    void makeCombo(const ChoiceParam& choice)
    {
        combo = EA::makeOwned<Widgets::ComboBox>();

        for (auto i = 0; i < choice.numChoices(); ++i)
            combo->addItem(choice.choiceName(i));

        combo->onChange = [this](int selected)
        {
            if (selected >= 0)
                writeGesture(static_cast<float>(selected));
        };

        control = combo.get();
    }

    void makeCheckbox()
    {
        checkbox = EA::makeOwned<Widgets::Checkbox>();
        checkbox->onChange = [this](bool on) { writeGesture(on ? 1.f : 0.f); };
        control = checkbox.get();
    }

    void writeGesture(float plain)
    {
        begin();
        param.setValue(plain);
        perform();
        end();
        pull();
    }

    void begin()
    {
        if (auto* listener = plugin.hostEditListener())
            listener->beginParameterEdit(index);
    }

    void perform()
    {
        if (auto* listener = plugin.hostEditListener())
            listener->performParameterEdit(index, param.getNormalized());
    }

    void end()
    {
        if (auto* listener = plugin.hostEditListener())
            listener->endParameterEdit(index);
    }

    Plugin& plugin;
    int index;
    Parameter& param;

    Widgets::Label name;
    Widgets::Label value;

    OwningPointer<Widgets::Slider> slider;
    OwningPointer<Widgets::ComboBox> combo;
    OwningPointer<Widgets::Checkbox> checkbox;
    Widgets::Component* control = nullptr;

    float shown = std::numeric_limits<float>::quiet_NaN();
    bool dragging = false;
};

class RowList final : public Widgets::Component
{
public:
    explicit RowList(Plugin& plugin)
    {
        auto count = plugin.parameters().size();
        rows.reserve(count);

        for (auto i = 0; i < count; ++i)
            addAndMakeVisible(*rows.add(EA::makeOwned<Row>(plugin, i)));

        setBounds({0.f, 0.f, 0.f, count * rowHeight});
    }

    void pull()
    {
        for (auto& row: rows)
            row->pull();
    }

    void resized() override
    {
        auto y = 0.f;

        for (auto& row: rows)
        {
            row->setBounds({0.f, y, getWidth(), rowHeight});
            y += rowHeight;
        }
    }

private:
    Vector<OwningPointer<Row>> rows;
};

class Root final : public Widgets::Component
{
public:
    explicit Root(Plugin& plugin)
        : rows(plugin)
    {
        empty.setColour(Widgets::defaultTheme().dimText);
        empty.setJustification(Widgets::Justification::Centred);

        scroll.setContent(rows);

        addAndMakeVisible(scroll);

        if (plugin.parameters().empty())
            addAndMakeVisible(empty);
    }

    void paint(Widgets::Graphics& g) override
    { g.fillAll(Widgets::defaultTheme().background); }

    void resized() override
    {
        auto area = getLocalBounds().inset(0.f, padding);
        scroll.setBounds(area);
        empty.setBounds(area);
    }

    RowList rows;

private:
    Widgets::ScrollPanel scroll;
    Widgets::Label empty {"No parameters"};
};
} // namespace

struct GenericEditor::Page final : Widgets::ComponentHost
{
    explicit Page(Plugin& plugin)
        : root(plugin)
    {
        setFontPointSize(13.f);
        setBackgroundColour(Widgets::defaultTheme().background);
        setRootComponent(root);
    }

    void startRefreshing()
    {
        root.rows.pull();
        timer.emplace([this] { root.rows.pull(); }, refreshHz);
    }

    void stopRefreshing() { timer.reset(); }

    Root root;
    std::optional<eacp::Threads::Timer> timer;
};

GenericEditor::GenericEditor(Plugin& pluginToUse)
    : plugin(pluginToUse)
{
}

GenericEditor::~GenericEditor() = default;

eacp::Graphics::View& GenericEditor::view()
{
    if (page == nullptr)
        page = EA::makeOwned<Page>(plugin);

    return *page;
}

EditorSize GenericEditor::initialSize() const
{
    auto rows = std::max(plugin.parameters().size(), 1);
    auto height = static_cast<int>(rows * rowHeight + 2.f * padding);

    return {pageWidth, std::clamp(height, minPageHeight, maxPageHeight)};
}

void GenericEditor::onAttached()
{
    view();
    page->startRefreshing();
}

void GenericEditor::onRemoved()
{
    if (page != nullptr)
        page->stopRefreshing();
}

} // namespace MakeASound
