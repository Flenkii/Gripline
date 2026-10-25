#include "video_options_xaml.h"
#include "rig_settings.h"
#include "support.h"
#include "video_options_text.h"
#include "video_settings.h"
#include "platform_link.h"

typedef struct {
    const char *property;
    const char *resource;
} VideoRow;

static const VideoRow drift_rows[] = {
    {"DriftMode", "EO_DriftMode"},
    {"DriftChaseSensitivity", "EO_ChaseSensitivity"},
    {"DriftChaseSpeed", "EO_ChaseSpeed"},
    {"DriftChaseRange", "EO_ChaseRange"},
    {"DriftDriverSensitivity", "EO_DriverSensitivity"},
    {"DriftDriverSpeed", "EO_DriverSpeed"},
    {"DriftDriverRange", "EO_DriverRange"},
};

static const VideoRow fov_rows[] = {
    {"FovChase", "EO_FovChase"}, {"FovFarChase", "EO_FovFarChase"}, {"FovDriver", "EO_FovDriver"},
    {"FovHood", "EO_FovHood"},   {"FovBumper", "EO_FovBumper"},
};

static const char brightness_preview_tag[] =
    "<StackPanel Margin=\"0, 0, 0, 6\" Orientation=\"Horizontal\" "
    "Style=\"{StaticResource "
    "A_Style_FrameworkElement_DefaultTransition_0}\" Background=\"Black\">";

unsigned video_options_page(const char *text, size_t length) {
    return text_find(text, length, "x:Class=\"ForzaUI.A_HudOptions\"") ? HUD_PAGE : VIDEO_PAGE;
}

int video_options_is_xaml(const char *text, size_t length) {
    return text_find(text, length, "x:Class=\"ForzaUI.A_VideoOptions\"") != NULL ||
           text_find(text, length, "x:Class=\"ForzaUI.A_HudOptions\"") != NULL;
}

typedef struct {
    char *bytes;
    size_t length, capacity;
    int valid;
} XamlBuffer;

static void append_bytes(XamlBuffer *buffer, const char *text, size_t length) {
    if (!buffer->valid || length >= buffer->capacity - buffer->length) {
        buffer->valid = 0;
        return;
    }
    memory_copy(buffer->bytes + buffer->length, text, length);
    buffer->length += length;
}

static void append_text(XamlBuffer *buffer, const char *text) {
    append_bytes(buffer, text, text_length(text));
}

static void append_attribute(XamlBuffer *buffer, const char *text) {
    for (; *text; ++text) {
        const char *escaped = *text == '&'   ? "&amp;"
                              : *text == '<' ? "&lt;"
                              : *text == '>' ? "&gt;"
                              : *text == '"' ? "&quot;"
                                             : NULL;
        if (escaped) {
            append_text(buffer, escaped);
        } else {
            append_bytes(buffer, text, 1);
        }
    }
}

static void append_heading(XamlBuffer *buffer, const VideoOptionsText *text, unsigned index) {
    append_attribute(buffer, text->camera);
    if (index) {
        if (index > 3) {
            append_text(buffer, " (");
            append_attribute(buffer, text->driver);
            append_text(buffer, ")");
        }
        append_text(buffer, " - ");
        append_attribute(buffer, text->parameters[(index - 1) % 3]);
    }
}

static void append_resources(XamlBuffer *buffer, const VideoOptionsText *text) {
    for (unsigned index = 0; index < ARRAY_COUNT(drift_rows); ++index) {
        append_text(buffer, "<local:TileMenuDataItem x:Key=\"");
        append_text(buffer, drift_rows[index].resource);
        append_text(buffer, "\" Heading=\"");
        append_heading(buffer, text, index);
        append_text(buffer, "\" Description=\"");
        append_attribute(buffer, text->descriptions[index ? 1 + (index - 1) % 3 : 0]);
        append_text(buffer, "\"/>");
    }
}

static void append_drift_rows(XamlBuffer *buffer, const VideoOptionsText *text) {
    append_text(buffer, "<local:SpinnerControl Tag=\"{StaticResource EO_DriftMode}\" "
                        "Style=\"{StaticResource A_Style_SpinnerControl_Default}\" Label=\"");
    append_attribute(buffer, text->camera);
    append_text(buffer, "\" SelectedIndex=\"{Binding DriftMode}\" "
                        "ItemsSource=\"{Binding DriftModes}\"/>");
    for (unsigned index = 1; index < ARRAY_COUNT(drift_rows); ++index) {
        append_text(buffer, "<Slider Value=\"{Binding ");
        append_text(buffer, drift_rows[index].property);
        append_text(buffer, "}\" Tag=\"{StaticResource ");
        append_text(buffer, drift_rows[index].resource);
        append_text(buffer, "}\" Style=\"{StaticResource A_Style_Slider_Options}\"/>");
    }
}

static const char fov_style[] =
    "<local:NumUnitConverter x:Key=\"EO_FovDegrees\" FractionDigits=\"1\" "
    "ShowUnit=\"False\"/>"
    "<Style x:Key=\"EO_FovSlider\" TargetType=\"Slider\" "
    "BasedOn=\"{StaticResource "
    "A_Style_Slider_Options}\">"
    "<Setter Property=\"Minimum\" Value=\"20\"/><Setter Property=\"Maximum\" "
    "Value=\"120\"/>"
    "<Setter Property=\"SmallChange\" Value=\"0.5\"/>"
    "<Setter Property=\"Template\"><Setter.Value><ControlTemplate "
    "TargetType=\"Slider\">"
    "<Grid "
    "FocusManager.IsFocusScope=\"True\"><Grid.ColumnDefinitions><"
    "ColumnDefinition/"
    "><ColumnDefinition/></Grid.ColumnDefinitions>"
    "<local:FZTextBlock x:Name=\"PART_Label\" Text=\"{Binding Tag.Heading, "
    "RelativeSource={RelativeSource TemplatedParent}}\" "
    "Style=\"{StaticResource "
    "A_Style_SliderControlText}\" HorizontalAlignment=\"Left\"/>"
    "<DockPanel Grid.Column=\"1\">"
    "<local:FZTextBlock DockPanel.Dock=\"Right\" Width=\"64\" "
    "TextAlignment=\"Right\" "
    "HorizontalAlignment=\"Right\" Text=\"{Binding Value, "
    "RelativeSource={RelativeSource "
    "TemplatedParent}, Converter={StaticResource EO_FovDegrees}}\" "
    "Style=\"{StaticResource "
    "A_Style_SliderControlText}\"/>"
    "<Grid Margin=\"32, 0, 8, 0\" Height=\"22\"><local:FZMaterial "
    "x:Name=\"PART_SliderMaterial\" "
    "Color=\"{StaticResource A_Color_MediumGreyText}\"/>"
    "<Track x:Name=\"PART_Track\"><Track.DecreaseRepeatButton><RepeatButton "
    "x:Name=\"Left\" "
    "Style=\"{StaticResource "
    "A_Style_RepeatButton_Left_Unfocused}\"/></Track.DecreaseRepeatButton></"
    "Track>"
    "</Grid>"
    "</DockPanel>"
    "</Grid>"
    "<ControlTemplate.Triggers>"
    "<DataTrigger Binding=\"{Binding RelativeSource={RelativeSource "
    "Mode=FindAncestor, "
    "AncestorType=ListBoxItem}, Path=IsKeyboardFocusWithin}\" Value=\"True\">"
    "<Setter TargetName=\"PART_SliderMaterial\" Property=\"Color\" "
    "Value=\"{StaticResource "
    "A_Color_Header_Background}\"/>"
    "<Setter TargetName=\"Left\" Property=\"Style\" Value=\"{StaticResource "
    "A_Style_RepeatButton_Left_Focused}\"/>"
    "</DataTrigger>"
    "</ControlTemplate.Triggers>"
    "</ControlTemplate></Setter.Value></Setter>"
    "</Style>";

static void append_fov_resources(XamlBuffer *buffer, const VideoOptionsText *text) {
    append_text(buffer, fov_style);
    for (unsigned index = 0; index < ARRAY_COUNT(fov_rows); ++index) {
        append_text(buffer, "<local:TileMenuDataItem x:Key=\"");
        append_text(buffer, fov_rows[index].resource);
        append_text(buffer, "\" Heading=\"FOV - ");
        append_attribute(buffer, text->fov_views[index]);
        append_text(buffer, "\" Description=\"");
        append_attribute(buffer, text->fov_description);
        append_text(buffer, "\"/>");
    }
}

static const char *const settings_properties[] = {"RealtimeShadows",
                                                  NULL,
                                                  "PhotoSamplesPreset",
                                                  NULL,
                                                  "MinimapOrientation",
                                                  "AutodriveCinematic",
                                                  "SkipChampionshipDialogue",
                                                  "SpawnLocation",
                                                  "CarChangePosition",
                                                  "LiveryResolution",
                                                  "LiveryMsaa",
                                                  "LiverySampling"};
static const char *const settings_collections[] = {"ShadowChoices",      NULL,
                                                   "PhotoChoices",       NULL,
                                                   "MinimapChoices",     "ToggleChoices",
                                                   "ToggleChoices",      "SpawnChoices",
                                                   "CarPositionChoices", "LiveryResolutionChoices",
                                                   "LiveryMsaaChoices",  "LiverySamplingChoices"};

static void append_settings_resources(XamlBuffer *buffer, const VideoOptionsText *text,
                                      unsigned first, unsigned end) {
    for (unsigned index = first; index < end; ++index) {
        if (!settings_properties[index]) {
            continue;
        }
        append_text(buffer, "<local:TileMenuDataItem x:Key=\"EO_");
        append_text(buffer, settings_properties[index]);
        append_text(buffer, "\" Heading=\"");
        append_attribute(buffer, text->settings_names[index]);
        append_text(buffer, "\" Description=\"");
        append_attribute(buffer, text->settings_descriptions[index]);
        append_text(buffer, "\"/>");
    }
}

static void append_settings_rows(XamlBuffer *buffer, const VideoOptionsText *text, unsigned first,
                                 unsigned end) {
    for (unsigned index = first; index < end; ++index) {
        if (!settings_properties[index]) {
            continue;
        }
        append_text(buffer, "<local:SpinnerControl Tag=\"{StaticResource EO_");
        append_text(buffer, settings_properties[index]);
        append_text(buffer, "}\" Style=\"{StaticResource "
                            "A_Style_SpinnerControl_Default}\" Label=\"");
        append_attribute(buffer, text->settings_names[index]);
        append_text(buffer, "\" SelectedIndex=\"{Binding ");
        append_text(buffer, settings_properties[index]);
        append_text(buffer, "}\" ItemsSource=\"{Binding ");
        append_text(buffer, settings_collections[index]);
        append_text(buffer, "}\"/>");
    }
}

static void append_fov_rows(XamlBuffer *buffer) {
    for (unsigned index = 0; index < ARRAY_COUNT(fov_rows); ++index) {
        append_text(buffer, "<Slider Value=\"{Binding ");
        append_text(buffer, fov_rows[index].property);
        append_text(buffer, "}\" Tag=\"{StaticResource ");
        append_text(buffer, fov_rows[index].resource);
        append_text(buffer, "}\" Style=\"{StaticResource EO_FovSlider}\"/>");
    }
}

/* ------------------------------------------------------------------ Lenkrad & Verkehr */

/* Same template as EO_FovSlider (label left, number right, bar between), with the number
   format and the range as parameters. */
static void append_number_slider_style(XamlBuffer *buffer, const char *key, const char *converter,
                                       const char *minimum, const char *maximum,
                                       const char *step) {
    append_text(buffer, "<Style x:Key=\"");
    append_text(buffer, key);
    append_text(buffer, "\" TargetType=\"Slider\" BasedOn=\"{StaticResource A_Style_Slider_Options}\">"
                        "<Setter Property=\"Minimum\" Value=\"");
    append_text(buffer, minimum);
    append_text(buffer, "\"/><Setter Property=\"Maximum\" Value=\"");
    append_text(buffer, maximum);
    append_text(buffer, "\"/><Setter Property=\"SmallChange\" Value=\"");
    append_text(buffer, step);
    append_text(buffer,
                "\"/><Setter Property=\"Template\"><Setter.Value><ControlTemplate "
                "TargetType=\"Slider\"><Grid FocusManager.IsFocusScope=\"True\"><Grid.ColumnDefinitions>"
                "<ColumnDefinition/><ColumnDefinition/></Grid.ColumnDefinitions>"
                "<local:FZTextBlock x:Name=\"PART_Label\" Text=\"{Binding Tag.Heading, "
                "RelativeSource={RelativeSource TemplatedParent}}\" Style=\"{StaticResource "
                "A_Style_SliderControlText}\" HorizontalAlignment=\"Left\"/>"
                "<DockPanel Grid.Column=\"1\"><local:FZTextBlock DockPanel.Dock=\"Right\" "
                "Width=\"64\" TextAlignment=\"Right\" HorizontalAlignment=\"Right\" "
                "Text=\"{Binding Value, RelativeSource={RelativeSource TemplatedParent}, "
                "Converter={StaticResource ");
    append_text(buffer, converter);
    append_text(buffer,
                "}}\" Style=\"{StaticResource A_Style_SliderControlText}\"/>"
                "<Grid Margin=\"32, 0, 8, 0\" Height=\"22\"><local:FZMaterial "
                "x:Name=\"PART_SliderMaterial\" Color=\"{StaticResource A_Color_MediumGreyText}\"/>"
                "<Track x:Name=\"PART_Track\"><Track.DecreaseRepeatButton><RepeatButton "
                "x:Name=\"Left\" Style=\"{StaticResource A_Style_RepeatButton_Left_Unfocused}\"/>"
                "</Track.DecreaseRepeatButton></Track></Grid></DockPanel></Grid>"
                "<ControlTemplate.Triggers><DataTrigger Binding=\"{Binding RelativeSource="
                "{RelativeSource Mode=FindAncestor, AncestorType=ListBoxItem}, "
                "Path=IsKeyboardFocusWithin}\" Value=\"True\"><Setter "
                "TargetName=\"PART_SliderMaterial\" Property=\"Color\" Value=\"{StaticResource "
                "A_Color_Header_Background}\"/><Setter TargetName=\"Left\" Property=\"Style\" "
                "Value=\"{StaticResource A_Style_RepeatButton_Left_Focused}\"/></DataTrigger>"
                "</ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>");
}

/* Percent sliders keep the game's own template (value shown as "70%"). */
static void append_percent_slider_style(XamlBuffer *buffer, const char *key, const char *maximum,
                                        const char *step) {
    append_text(buffer, "<Style x:Key=\"");
    append_text(buffer, key);
    append_text(buffer, "\" TargetType=\"Slider\" BasedOn=\"{StaticResource A_Style_Slider_Options}\">"
                        "<Setter Property=\"Maximum\" Value=\"");
    append_text(buffer, maximum);
    append_text(buffer, "\"/><Setter Property=\"SmallChange\" Value=\"");
    append_text(buffer, step);
    append_text(buffer, "\"/></Style>");
}

static void append_rig_resources(XamlBuffer *buffer, unsigned char *image) {
    append_text(buffer, "<local:NumUnitConverter x:Key=\"EO_RigNumber0\" FractionDigits=\"0\" "
                        "ShowUnit=\"False\"/><local:NumUnitConverter x:Key=\"EO_RigNumber1\" "
                        "FractionDigits=\"1\" ShowUnit=\"False\"/>");
    append_number_slider_style(buffer, "EO_RigMilliseconds", "EO_RigNumber0", "0", "200", "5");
    append_number_slider_style(buffer, "EO_RigLock", "EO_RigNumber0", "30", "450", "10");
    append_number_slider_style(buffer, "EO_RigRotation", "EO_RigNumber0", "180", "2520", "30");
    append_number_slider_style(buffer, "EO_RigCurve", "EO_RigNumber1", "1", "6", "0.1");
    append_number_slider_style(buffer, "EO_KeyTime", "EO_RigNumber0", "0", "1000", "10");
    append_number_slider_style(buffer, "EO_GripViewBack", "EO_RigNumber0", "-20", "60", "1");
    append_number_slider_style(buffer, "EO_GripViewUp", "EO_RigNumber0", "-20", "30", "1");
    append_percent_slider_style(buffer, "EO_RigPercent200", "2", "0.05");
    append_percent_slider_style(buffer, "EO_RigPercent100", "1", "0.05");
    append_percent_slider_style(buffer, "EO_RigPercent50", "0.5", "0.01");
    for (unsigned position = 0; position < rig_setting_menu_count(); ++position) {
        unsigned id = rig_setting_order(position);
        const RigSettingInfo *info = rig_setting_info(id);
        append_text(buffer, "<local:TileMenuDataItem x:Key=\"EO_");
        append_text(buffer, info->property);
        append_text(buffer, "\" Heading=\"");
        append_attribute(buffer, rig_setting_heading(image, id));
        append_text(buffer, "\" Description=\"");
        append_attribute(buffer, rig_setting_description(image, id));
        append_text(buffer, "\"/>");
    }
}

static void append_rig_rows(XamlBuffer *buffer, unsigned char *image, int name_first) {
    for (unsigned position = 0; position < rig_setting_menu_count(); ++position) {
        unsigned id = rig_setting_order(position);
        const RigSettingInfo *info = rig_setting_info(id);
        const char *name = name_first && position == 0 ? " x:Name=\"FirstItem\"" : "";
        if (rig_kind_is_choice(info->kind)) {
            append_text(buffer, "<local:SpinnerControl");
            append_text(buffer, name);
            append_text(buffer, " Tag=\"{StaticResource EO_");
            append_text(buffer, info->property);
            append_text(buffer, "}\" Style=\"{StaticResource A_Style_SpinnerControl_Default}\" "
                                "Label=\"");
            append_attribute(buffer, rig_setting_heading(image, id));
            append_text(buffer, "\" SelectedIndex=\"{Binding ");
            append_text(buffer, info->property);
            append_text(buffer, "}\" ItemsSource=\"{Binding ");
            append_text(buffer, rig_kind_collection(info->kind));
            append_text(buffer, "}\"/>");
        } else {
            append_text(buffer, "<Slider");
            append_text(buffer, name);
            append_text(buffer, " Value=\"{Binding ");
            append_text(buffer, info->property);
            append_text(buffer, "}\" Tag=\"{StaticResource EO_");
            append_text(buffer, info->property);
            append_text(buffer, "}\" Style=\"{StaticResource ");
            append_text(buffer, info->style);
            append_text(buffer, "}\"/>");
        }
    }
}

static size_t extend_hud_xaml(unsigned char *image, const char *text, size_t length, char *output,
                              size_t capacity) {
    const char *resources = text_find(text, length, "</ResourceDictionary>");
    const char *list = text_find(text, length, "<local:CommonListBox x:Name=\"OptionsList\"");
    const char *end =
        list ? text_find(list, length - (size_t)(list - text), "</local:CommonListBox>") : NULL;
    static const char native_width[] = "Width=\"688\"";
    const char *opening_end = list && end ? text_find(list, (size_t)(end - list), ">") : NULL;
    const char *width =
        opening_end ? text_find(list, (size_t)(opening_end - list), native_width) : NULL;
    if (!capacity || !resources || !end || !width || resources >= width || width >= end ||
        text_find(text, length, "EO_MinimapOrientation")) {
        return 0;
    }
    const VideoOptionsText *localized = video_options_text(image);
    XamlBuffer buffer = {output, 0, capacity, 1};
    if (length < 3 || !memory_equal(text, "\xef\xbb\xbf", 3)) {
        append_bytes(&buffer, "\xef\xbb\xbf", 3);
    }
    for (size_t index = 0; index < length && buffer.valid;) {
        if (text + index == resources) {
            append_resources(&buffer, localized);
            append_fov_resources(&buffer, localized);
            append_settings_resources(&buffer, localized, 4, 9);
            if (!platform_page_active()) append_rig_resources(&buffer, image);
        }
        if (text + index == end) {
            append_drift_rows(&buffer, localized);
            append_fov_rows(&buffer);
            append_settings_rows(&buffer, localized, 4, 9);
            if (!platform_page_active()) append_rig_rows(&buffer, image, 0);
        }
        if (text + index == width) {
            append_text(&buffer, "Width=\"1000\"");
            index += sizeof(native_width) - 1;
        } else {
            append_bytes(&buffer, text + index, 1);
            ++index;
        }
    }
    if (!buffer.valid) {
        return 0;
    }
    buffer.bytes[buffer.length] = 0;
    return buffer.length;
}

size_t video_options_extend_xaml(unsigned char *game_image, const char *text, size_t length,
                                 char *output, size_t capacity) {
    if (text_find(text, length, "x:Class=\"ForzaUI.A_HudOptions\"")) {
        return extend_hud_xaml(game_image, text, length, output, capacity);
    }
    const char *preview = text_find(text, length, brightness_preview_tag);
    const char *resources = text_find(text, length, "</ResourceDictionary>");
    const char *list = text_find(text, length, "<local:CommonListBox x:Name=\"OptionsList\"");
    const char *rows =
        list ? text_find(list, length - (size_t)(list - text), "</local:CommonListBox>") : NULL;
    const char *height =
        list && rows ? text_find(list, (size_t)(rows - list), "Height=\"110\"") : NULL;
    static const char description_height_tag[] =
        "Height=\"100\" Width=\"806\" Template=\"{StaticResource "
        "A_ControlTemplate_DescriptionPanelContent}\"";
    const char *description = text_find(text, length, description_height_tag);
    const char *subtitles =
        list && rows ? text_find(list, (size_t)(rows - list), "<local:SpinnerControl") : NULL;
    if (!capacity || !subtitles || !text_find(text, length, "x:Class=\"ForzaUI.A_VideoOptions\"") ||
        !preview || !resources || !height || !rows || resources >= height || height >= rows ||
        text_find(text, length, "EO_RealtimeShadows")) {
        return 0;
    }
    const VideoOptionsText *localized = video_options_text(game_image);
    XamlBuffer buffer = {output, 0, capacity, 1};
    // FH2's XML reader only enables UTF-8 decoding after seeing its byte-order
    // mark.
    static const char utf8_bom[] = "\xef\xbb\xbf";
    if (length < 3 || !memory_equal(text, utf8_bom, 3)) {
        append_bytes(&buffer, utf8_bom, 3);
    }
    for (size_t index = 0; index < length && buffer.valid;) {
        if (text + index == resources) {
            append_settings_resources(&buffer, localized, 0, 4);
            append_settings_resources(&buffer, localized, 9, 12);
        }
        if (text + index == rows) {
            append_settings_rows(&buffer, localized, 0, 4);
            append_settings_rows(&buffer, localized, 9, 12);
        }
        if (text + index == preview) {
            append_text(&buffer, "<StackPanel Width=\"806\" HorizontalAlignment=\"Center\" "
                                 "Margin=\"0, 0, "
                                 "0, 6\" Orientation=\"Horizontal\" Style=\"{StaticResource "
                                 "A_Style_FrameworkElement_DefaultTransition_0}\" "
                                 "Background=\"Black\">");
            index += sizeof(brightness_preview_tag) - 1;
        } else if (text + index == description) {
            append_text(&buffer, "Height=\"100\"");
            index += sizeof("Height=\"100\"") - 1;
        } else if (text + index == height) {
            append_text(&buffer, "Height=\"275\"");
            index += 12;
        } else {
            append_bytes(&buffer, text + index, 1);
            ++index;
        }
    }
    if (!buffer.valid) {
        return 0;
    }
    output[buffer.length] = 0;
    return buffer.length;
}

/* ------------------------------------------------------------------ Gripline tile page */

/* The page behind the GRIPLINE tile of the pause menu (FH2 Mod Platform, platform_link.c):
   the same rows as on the HUD page, bound to the same properties (every platform page is the
   game's HUD options page loading another file). The marker comment makes get_resource()
   register Gripline's properties when this page is opened before the HUD page. */
int video_options_gripline_page(unsigned char *image, char *resources, size_t resources_capacity,
                                char *content, size_t content_capacity) {
    XamlBuffer r = {resources, 0, resources_capacity, 1};
    append_text(&r, GRIPLINE_PAGE_MARKER);
    append_rig_resources(&r, image);
    XamlBuffer c = {content, 0, content_capacity, 1};
    append_text(&c,
                "<StackPanel Name=\"BasicOptions\" Orientation=\"Vertical\" VerticalAlignment=\"Center\" "
                "HorizontalAlignment=\"Center\"><StackPanel Orientation=\"Horizontal\" Margin=\"0,0,0,6\" "
                "Style=\"{StaticResource A_Style_FrameworkElement_DefaultTransition_0}\"><ContentControl "
                "Tag=\"" GRIPLINE_VERSION "\" Content=\"GRIPLINE\" Style=\"{StaticResource "
                "A_Style_ContentControl_ScreenTitleBlock}\" Focusable=\"False\"/></StackPanel>"
                "<StackPanel Orientation=\"Horizontal\" Style=\"{StaticResource "
                "A_Style_FrameworkElement_DefaultTransition_0}\"><Grid Margin=\"0, 0, 6, 0\" "
                "local:UIPageTransitionManager.TransitionIndex=\"0\"><local:FZMaterial Color=\""
                "{StaticResource A_Color_TileSelected_Text}\"><local:CommonListBox x:Name=\"OptionsList\" "
                "KeyboardNavigation.DirectionalNavigation=\"Contained\" Style=\"{StaticResource "
                "A_Style_ListBox_List_Default}\" ItemContainerStyle=\"{StaticResource "
                "A_Style_ListBoxItem_List_Default}\" Width=\"1000\" Height=\"550\" Margin=\"0,0,0,0\" "
                "HorizontalAlignment=\"Stretch\" VerticalAlignment=\"Stretch\">");
    append_rig_rows(&c, image, 1);
    append_text(&c, "</local:CommonListBox></local:FZMaterial></Grid><ContentControl Content=\"{Binding "
                    "Tag.Description}\" DataContext=\"{Binding SelectedItem, ElementName=OptionsList}\" "
                    "Style=\"{StaticResource A_Style_DescriptionPanel}\" "
                    "local:UIPageTransitionManager.TransitionIndex=\"1\"/></StackPanel></StackPanel>");
    if (!r.valid || !c.valid) return 0;
    r.bytes[r.length] = 0;
    c.bytes[c.length] = 0;
    return 1;
}
