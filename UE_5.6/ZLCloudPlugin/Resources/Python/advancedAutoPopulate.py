"""
@name: Advanced Auto-Populate
@description: Drive the OmniStream V2 schema editor's auto-populate from a checklist of levels and module options.
@tags: omnistream,schema,auto-populate
"""

import os
import sys
import importlib
import difflib
import json
from pathlib import Path

import unreal

# Reuse the qt styling helpers and PySide6 dependency installer from the
# ZLEditorTools python package - both ship inside the same project.
_ZLEDITORTOOLS_PY_DIR = os.path.abspath(os.path.join(
    unreal.Paths.project_plugins_dir(),
    "ZLEditorTools", "Resources", "Python"
))
if _ZLEDITORTOOLS_PY_DIR not in sys.path:
    sys.path.append(_ZLEDITORTOOLS_PY_DIR)

import python_dependency_installer as dep_installer
importlib.reload(dep_installer)

try:
    from PySide6.QtWidgets import (
        QWidget, QVBoxLayout, QHBoxLayout, QLabel, QGroupBox, QCheckBox,
        QPushButton, QScrollArea, QListWidget, QListWidgetItem, QMessageBox,
        QSizePolicy, QProgressBar, QApplication, QDialog, QPlainTextEdit, QSplitter, QTextEdit,
        QStackedWidget
    )
    from PySide6.QtCore import Qt
    from PySide6.QtGui import QColor, QTextCharFormat, QTextCursor, QTextFormat
except ImportError:
    print("\nPySide6 is not installed. Installing...")
    dep_installer.install_python_dependency("PySide6")
    sys.exit(1)

import qt_window_styling
importlib.reload(qt_window_styling)


# ---------------------------------------------------------------------------
# Level discovery helpers (mirrors the logic in
# ZLEditorTools/Resources/Python/packagedLevelsList.py, kept local here so
# this window is self-contained).
# ---------------------------------------------------------------------------

def _get_level_assets():
    asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
    far_filter = unreal.ARFilter(
        class_names=["World"], package_paths=["/Game"], recursive_paths=True
    )
    assets = asset_registry.get_assets(far_filter)
    return sorted(str(asset.package_name) for asset in assets)


def _get_maps_in_cook_ini():
    config_path = os.path.join(unreal.Paths.project_config_dir(), "DefaultEditor.ini")
    maps = set()
    if os.path.exists(config_path):
        with open(config_path, "r") as f:
            for line in f:
                if line.startswith("+Map="):
                    maps.add(line.split("=", 1)[1].strip())
    return maps


def _get_default_maps():
    engine_config_path = os.path.join(unreal.Paths.project_config_dir(), "DefaultEngine.ini")
    default_maps = set()
    game_default_map = ""
    editor_startup_map = ""

    if os.path.exists(engine_config_path):
        in_section = False
        with open(engine_config_path, "r") as f:
            for line in f:
                if line.strip() == "[/Script/EngineSettings.GameMapsSettings]":
                    in_section = True
                    continue
                elif line.startswith("["):
                    in_section = False

                if in_section:
                    if line.startswith("GameDefaultMap="):
                        game_default_map = line.split("=", 1)[1].strip().split('.')[0]
                    elif line.startswith("EditorStartupMap="):
                        editor_startup_map = line.split("=", 1)[1].strip().split('.')[0]

    if game_default_map:
        default_maps.add(game_default_map)
        _add_world_dependencies(game_default_map, default_maps)

    if editor_startup_map:
        default_maps.add(editor_startup_map)
        _add_world_dependencies(editor_startup_map, default_maps)

    return default_maps


def _add_world_dependencies(asset_path, dependency_set):
    asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
    options = unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True,
        include_hard_package_references=True
    )
    dependencies = asset_registry.get_dependencies(asset_path, options)
    if not dependencies:
        return
    for dep in dependencies:
        if dep in dependency_set:
            continue
        for asset in asset_registry.get_assets_by_package_name(dep):
            if asset and asset.get_class().get_name() == "World":
                dependency_set.add(str(dep))


def _get_default_ticked_levels():
    return _get_default_maps().union(_get_maps_in_cook_ini())


def _get_model_entries_from_dimebake_files():
    project_root = unreal.Paths.project_dir()
    if not project_root:
        return []

    skip_dirs = {
        ".git", ".vs", ".idea", "__pycache__",
        "Binaries", "Intermediate", "Saved", "DerivedDataCache"
    }
    entries_by_key = {}

    for root, dirs, files in os.walk(project_root):
        dirs[:] = [d for d in dirs if d not in skip_dirs]

        for filename in files:
            lower_name = filename.lower()
            if lower_name != "configuration.dimebake" and lower_name != "configurationdata.dimebake":
                continue

            file_path = os.path.join(root, filename)
            parts = Path(file_path).parts
            config_idx = -1
            for idx, part in enumerate(parts):
                if part.lower() == "configuration":
                    config_idx = idx

            if config_idx <= 0:
                continue

            model_name = str(parts[config_idx - 1]).strip()
            if not model_name:
                continue

            configuration_dir = Path(file_path).parent
            model_root_dir = configuration_dir.parent
            sibling_umaps = sorted(model_root_dir.glob("*.umap"))

            accepted_model_name = ""
            if sibling_umaps:
                accepted_model_name = sibling_umaps[0].stem.strip()

            display_name = accepted_model_name if accepted_model_name else model_name

            encoded_selector_value = "MODEL::{query}::{accepted}".format(
                query=model_name,
                accepted=accepted_model_name
            ) if accepted_model_name else "FALLBACK::{}".format(model_name)

            dedupe_key = "{}|{}".format(encoded_selector_value.lower(), display_name.lower())
            entries_by_key[dedupe_key] = {
                "display_name": display_name,
                "selector_value": encoded_selector_value
            }

    return sorted(entries_by_key.values(), key=lambda value: value["display_name"].lower())


def _is_packaged_levels_list_available():
    # Backward compatible: this helper may not exist until C++ plugin binaries are rebuilt.
    has_editor_tools_api = getattr(
        unreal.ZLOmniStreamAutoPopulateLibrary,
        "is_zleditor_tools_available",
        None
    )
    if callable(has_editor_tools_api):
        if not has_editor_tools_api():
            return False

    packaged_levels_script = os.path.join(
        unreal.Paths.project_plugins_dir(),
        "ZLEditorTools", "Resources", "Python", "packagedLevelsList.py"
    )
    return os.path.exists(packaged_levels_script)


def _open_packaged_levels_list(parent_widget):
    if not _is_packaged_levels_list_available():
        QMessageBox.information(
            parent_widget,
            "ZLEditorTools unavailable",
            "Packaged Levels List is unavailable because ZLEditorTools is not present in this project/build."
        )
        return

    try:
        zleditor_tools_py = os.path.join(
            unreal.Paths.project_plugins_dir(),
            "ZLEditorTools", "Resources", "Python"
        )
        if zleditor_tools_py not in sys.path:
            sys.path.append(zleditor_tools_py)

        import packagedLevelsList
        importlib.reload(packagedLevelsList)
        packagedLevelsList.open_window()
    except Exception as exc:
        QMessageBox.warning(
            parent_widget,
            "Failed to open Packaged Levels List",
            str(exc)
        )


def _resolve_baseline_path(loaded_path):
    if not loaded_path:
        return None

    loaded_path_obj = Path(loaded_path)
    suffix = loaded_path_obj.suffix.lower()

    if suffix == ".zlschema" and loaded_path_obj.exists():
        return str(loaded_path_obj)

    if suffix == ".uasset":
        sibling_schema = loaded_path_obj.with_suffix(".zlschema")
        if sibling_schema.exists():
            return str(sibling_schema)

    return None


def _run_preview_step(selected_options, selected_levels, additive, advanced_settings_json):
    if hasattr(unreal.ZLOmniStreamAutoPopulateLibrary, "preview_auto_populate_with_settings"):
        result = unreal.ZLOmniStreamAutoPopulateLibrary.preview_auto_populate_with_settings(
            selected_options, selected_levels, additive, advanced_settings_json
        )
    else:
        result = unreal.ZLOmniStreamAutoPopulateLibrary.preview_auto_populate(
            selected_options, selected_levels, additive
        )

    ok = False
    preview_text = ""

    if isinstance(result, tuple):
        ok = bool(result[0]) if len(result) > 0 else False
        second = result[1] if len(result) > 1 else ""
        preview_text = str(second) if second is not None else ""
    elif isinstance(result, str):
        preview_text = result
        ok = bool(preview_text)
    else:
        ok = bool(result)

    if ok and not preview_text:
        pending = unreal.ZLOmniStreamAutoPopulateLibrary.get_pending_preview_schema_text()
        preview_text = str(pending) if pending is not None else ""

    return ok, preview_text


def _run_apply_step(selected_options, selected_levels, additive, advanced_settings_json):
    if hasattr(unreal.ZLOmniStreamAutoPopulateLibrary, "run_auto_populate_with_settings"):
        return unreal.ZLOmniStreamAutoPopulateLibrary.run_auto_populate_with_settings(
            selected_options, selected_levels, additive, advanced_settings_json
        )
    return unreal.ZLOmniStreamAutoPopulateLibrary.run_auto_populate(
        selected_options, selected_levels, additive
    )


def _canonicalize_schema_for_diff(schema_text):
    try:
        parsed = json.loads(schema_text)
    except Exception:
        return schema_text

    def _normalize(value, parent_key=None):
        if isinstance(value, dict):
            normalized = {}

            if parent_key == "properties":
                keys = sorted(value.keys(), key=lambda k: str(k).lower())
            else:
                keys = list(value.keys())

            for key in keys:
                child = value[key]
                if key == "enum" and isinstance(child, list):
                    normalized[key] = sorted(child, key=lambda item: str(item).lower())
                else:
                    normalized[key] = _normalize(child, key)
            return normalized

        if isinstance(value, list):
            return [_normalize(item, parent_key) for item in value]

        return value

    canonical = _normalize(parsed)
    return json.dumps(canonical, indent=2, ensure_ascii=False)


def _persist_selected_auto_populate_options(selected_options):
    if hasattr(unreal.ZLOmniStreamAutoPopulateLibrary, "set_selected_auto_populate_options"):
        return unreal.ZLOmniStreamAutoPopulateLibrary.set_selected_auto_populate_options(selected_options)

    return False


class DiffConfirmDialog(QDialog):
    def __init__(self, baseline_path, baseline_text, proposed_text, parent=None):
        super(DiffConfirmDialog, self).__init__(parent)
        self.setWindowTitle("Auto-Populate Diff")
        self.setModal(True)
        self.setMinimumSize(1200, 700)
        self._baseline_text = baseline_text
        self._proposed_text = proposed_text

        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(8)

        headline = QLabel(
            "Review the proposed schema changes.\n"
            "Select 'Yes' to apply or 'No' to discard."
        )
        headline.setWordWrap(True)
        layout.addWidget(headline)

        compare_row = QHBoxLayout()
        compare_row.setContentsMargins(0, 0, 0, 0)
        compare_row.addWidget(QLabel("Original schema: {}".format(baseline_path)))
        compare_row.addStretch(1)
        proposed_controls = QVBoxLayout()
        proposed_controls.setContentsMargins(0, 0, 0, 0)
        proposed_controls.setSpacing(2)
        self.unified_diff_checkbox = QCheckBox("Show as unified diff")
        self.unified_diff_checkbox.toggled.connect(self._on_view_mode_toggled)
        proposed_controls.addWidget(self.unified_diff_checkbox, alignment=Qt.AlignRight)
        proposed_controls.addWidget(QLabel("Proposed auto-populated schema"), alignment=Qt.AlignRight)
        compare_row.addLayout(proposed_controls)
        layout.addLayout(compare_row)

        self.view_stack = QStackedWidget()
        layout.addWidget(self.view_stack, stretch=1)

        side_by_side_container = QWidget()
        side_by_side_layout = QVBoxLayout(side_by_side_container)
        side_by_side_layout.setContentsMargins(0, 0, 0, 0)
        side_by_side_layout.setSpacing(0)

        editors_splitter = QSplitter(Qt.Horizontal)
        self.original_view = QPlainTextEdit()
        self.original_view.setReadOnly(True)
        self.original_view.setLineWrapMode(QPlainTextEdit.NoWrap)
        self.original_view.setPlainText(baseline_text)
        editors_splitter.addWidget(self.original_view)

        self.proposed_view = QPlainTextEdit()
        self.proposed_view.setReadOnly(True)
        self.proposed_view.setLineWrapMode(QPlainTextEdit.NoWrap)
        self.proposed_view.setPlainText(proposed_text)
        editors_splitter.addWidget(self.proposed_view)

        editors_splitter.setStretchFactor(0, 1)
        editors_splitter.setStretchFactor(1, 1)
        side_by_side_layout.addWidget(editors_splitter)
        self.view_stack.addWidget(side_by_side_container)
        self._apply_side_by_side_diff_highlights(baseline_text, proposed_text)

        unified_container = QWidget()
        unified_layout = QVBoxLayout(unified_container)
        unified_layout.setContentsMargins(0, 0, 0, 0)
        unified_layout.setSpacing(0)
        self.unified_diff_view = QPlainTextEdit()
        self.unified_diff_view.setReadOnly(True)
        self.unified_diff_view.setLineWrapMode(QPlainTextEdit.NoWrap)
        unified_layout.addWidget(self.unified_diff_view)
        self.view_stack.addWidget(unified_container)
        self._rebuild_unified_diff_view()

        self.view_stack.setCurrentIndex(0)

        if baseline_text == proposed_text:
            no_diff_label = QLabel("No textual differences detected.")
            layout.addWidget(no_diff_label)

        button_bar = QHBoxLayout()
        button_bar.addWidget(QLabel("Confirm changes:"))
        button_bar.addStretch(1)
        self.no_button = QPushButton("No")
        self.yes_button = QPushButton("Yes")
        self.no_button.clicked.connect(self.reject)
        self.yes_button.clicked.connect(self.accept)
        button_bar.addWidget(self.no_button)
        button_bar.addWidget(self.yes_button)
        layout.addLayout(button_bar)

    def _on_view_mode_toggled(self, is_checked):
        self.view_stack.setCurrentIndex(1 if is_checked else 0)

    def _rebuild_unified_diff_view(self):
        diff_lines = list(difflib.unified_diff(
            self._baseline_text.splitlines(),
            self._proposed_text.splitlines(),
            fromfile="original_schema.zlschema",
            tofile="proposed_auto_populate.zlschema",
            lineterm=""
        ))
        if not diff_lines:
            diff_lines = ["No textual differences detected."]

        unified_text = "\n".join(diff_lines)
        self.unified_diff_view.setPlainText(unified_text)

        removed_ranges = []
        added_ranges = []
        for idx, line in enumerate(diff_lines):
            if line.startswith("---") or line.startswith("+++") or line.startswith("@@"):
                continue
            if line.startswith("-"):
                removed_ranges.append((idx, idx + 1))
            elif line.startswith("+"):
                added_ranges.append((idx, idx + 1))

        unified_selections = []
        unified_selections.extend(
            self._build_line_selections(self.unified_diff_view, removed_ranges, QColor(255, 220, 220))
        )
        unified_selections.extend(
            self._build_line_selections(self.unified_diff_view, added_ranges, QColor(220, 255, 220))
        )
        self.unified_diff_view.setExtraSelections(unified_selections)

    def _apply_side_by_side_diff_highlights(self, baseline_text, proposed_text):
        baseline_lines = baseline_text.splitlines()
        proposed_lines = proposed_text.splitlines()
        matcher = difflib.SequenceMatcher(a=baseline_lines, b=proposed_lines)

        baseline_removed_ranges = []
        proposed_added_ranges = []

        for tag, i1, i2, j1, j2 in matcher.get_opcodes():
            if tag == "delete":
                baseline_removed_ranges.append((i1, i2))
            elif tag == "insert":
                proposed_added_ranges.append((j1, j2))
            elif tag == "replace":
                baseline_removed_ranges.append((i1, i2))
                proposed_added_ranges.append((j1, j2))

        self.original_view.setExtraSelections(
            self._build_line_selections(self.original_view, baseline_removed_ranges, QColor(255, 220, 220))
        )
        self.proposed_view.setExtraSelections(
            self._build_line_selections(self.proposed_view, proposed_added_ranges, QColor(220, 255, 220))
        )

    def _build_line_selections(self, text_edit, line_ranges, background_color):
        selections = []
        base_format = QTextCharFormat()
        base_format.setBackground(background_color)
        base_format.setForeground(QColor(20, 20, 20))
        base_format.setProperty(QTextFormat.FullWidthSelection, True)

        document = text_edit.document()
        for start_line, end_line in line_ranges:
            for line_index in range(start_line, end_line):
                block = document.findBlockByNumber(line_index)
                if not block.isValid():
                    continue

                cursor = QTextCursor(block)
                cursor.select(QTextCursor.LineUnderCursor)

                selection = QTextEdit.ExtraSelection()
                selection.cursor = cursor
                selection.format = QTextCharFormat(base_format)
                selections.append(selection)

        return selections


# ---------------------------------------------------------------------------
# Window
# ---------------------------------------------------------------------------

class AdvancedAutoPopulateWindow(QWidget):
    window = None

    def __init__(self, parent=None):
        super(AdvancedAutoPopulateWindow, self).__init__(parent)

        self.level_assets = _get_level_assets()
        self.default_ticked_levels = _get_default_ticked_levels()
        self.module_descs = list(unreal.ZLOmniStreamAutoPopulateLibrary.get_auto_populate_modules())
        currently_selected = unreal.ZLOmniStreamAutoPopulateLibrary.get_currently_selected_options()
        self.currently_selected_options = set(str(opt) for opt in currently_selected)

        self.module_option_checkboxes = []
        self.option_checkbox_map = {}
        self.option_advanced_groups = {}
        self.option_advanced_controls = {}
        self.discovered_dime_model_entries = _get_model_entries_from_dimebake_files()
        self._suppress_close_save_prompt = False

        self._build_ui()
        self._snapshot_current_settings()
        self.adjustSize()
        self.resize(500, self.height())

    # ------------------------------------------------------------------
    @staticmethod
    def _to_py_string(value):
        if value is None:
            return ""
        return str(value)

    @staticmethod
    def _is_main_wrapper_level(level_package):
        return AdvancedAutoPopulateWindow._to_py_string(level_package).strip().lower() == "/game/main"

    @staticmethod
    def _get_short_level_name(level_package):
        value = AdvancedAutoPopulateWindow._to_py_string(level_package).strip()
        if not value:
            return ""
        return value.rsplit("/", 1)[-1]

    @staticmethod
    def _debug_log(message):
        unreal.log("[AdvancedAutoPopulate][ModelLevels] {}".format(message))

    @staticmethod
    def _is_model_config_selector(option_name, selector_id):
        return (
            AdvancedAutoPopulateWindow._to_py_string(option_name).strip().lower() == "model configuration"
            and AdvancedAutoPopulateWindow._to_py_string(selector_id).strip().lower() == "modelconfiglevels"
        )

    @staticmethod
    def _set_all_items_check_state(list_widget, check_state):
        if not list_widget:
            return
        for row in range(list_widget.count()):
            item = list_widget.item(row)
            item.setCheckState(check_state)

    def _set_all_module_options_check_state(self, checked):
        for checkbox in self.module_option_checkboxes:
            checkbox.setChecked(checked)

    def _get_matching_levels_for_selector(self, actor_class_path, component_class_path):
        self._debug_log(
            "Starting level scan. actor_class='{}', component_class='{}', total_levels={}".format(
                actor_class_path, component_class_path, len(self.level_assets)
            )
        )
        matched_levels = []
        per_level_root_objects = {}

        def _add_level_if_unique(level_name, source):
            level_value = self._to_py_string(level_name)
            if not level_value:
                return
            if level_value not in matched_levels:
                matched_levels.append(level_value)
                self._debug_log("Added level '{}' from {}".format(level_value, source))
            else:
                self._debug_log("Skipped duplicate level '{}' from {}".format(level_value, source))

        if hasattr(unreal.ZLOmniStreamAutoPopulateLibrary, "get_level_actor_display_entries_by_class_requirements"):
            actor_entries = list(
                unreal.ZLOmniStreamAutoPopulateLibrary.get_level_actor_display_entries_by_class_requirements(
                    self.level_assets,
                    actor_class_path,
                    component_class_path
                )
            )
            for entry in actor_entries:
                _add_level_if_unique(self._get_desc_field(entry, "LevelPackageName", ""), "global actor-entry scan")

        if hasattr(unreal.ZLOmniStreamAutoPopulateLibrary, "filter_levels_by_class_requirements"):
            filtered_levels = list(
                unreal.ZLOmniStreamAutoPopulateLibrary.filter_levels_by_class_requirements(
                    self.level_assets,
                    actor_class_path,
                    component_class_path
                )
            )
            for level in filtered_levels:
                _add_level_if_unique(level, "global level-filter scan")

            # Also validate each level independently so sub-levels are not lost
            # when aggregate queries collapse results to wrapper maps.
            for level in self.level_assets:
                level_name = self._to_py_string(level)
                if not level_name:
                    continue
                self._debug_log("Checking level '{}'".format(level_name))

                if hasattr(unreal.ZLOmniStreamAutoPopulateLibrary, "get_level_actor_display_entries_by_class_requirements"):
                    per_level_entries = list(
                        unreal.ZLOmniStreamAutoPopulateLibrary.get_level_actor_display_entries_by_class_requirements(
                            [level_name],
                            actor_class_path,
                            component_class_path
                        )
                    )
                    zlroot_objects = []
                    for entry in per_level_entries:
                        actor_name = self._to_py_string(self._get_desc_field(entry, "ActorDisplayName", ""))
                        if actor_name and actor_name not in zlroot_objects:
                            zlroot_objects.append(actor_name)
                    per_level_root_objects[level_name] = zlroot_objects
                    self._debug_log(
                        "Level '{}' valid ZLRootObjects: {}".format(
                            level_name,
                            zlroot_objects if zlroot_objects else "[]"
                        )
                    )

                per_level_result = list(
                    unreal.ZLOmniStreamAutoPopulateLibrary.filter_levels_by_class_requirements(
                        [level_name],
                        actor_class_path,
                        component_class_path
                    )
                )
                self._debug_log("Level '{}' filter result: {}".format(level_name, per_level_result))
                if per_level_result:
                    _add_level_if_unique(level_name, "per-level validation")

        # In some projects, once a product map is streamed by /Game/Main, the matching
        # component can be reported only against /Game/Main. If that happens, infer
        # likely product sub-levels from the root object names discovered in Main.
        main_level_key = None
        for level_key in per_level_root_objects.keys():
            if self._is_main_wrapper_level(level_key):
                main_level_key = level_key
                break
        if main_level_key:
            main_root_objects = per_level_root_objects.get(main_level_key, [])
            if main_root_objects:
                self._debug_log(
                    "Main-level roots detected for inference: {}".format(main_root_objects)
                )
                for candidate_level in self.level_assets:
                    candidate_level_name = self._to_py_string(candidate_level)
                    if (not candidate_level_name or
                            self._is_main_wrapper_level(candidate_level_name) or
                            candidate_level_name in matched_levels):
                        continue

                    short_name = self._get_short_level_name(candidate_level_name).lower()
                    if not short_name:
                        continue

                    inferred_match = False
                    for root_name in main_root_objects:
                        root_lower = self._to_py_string(root_name).lower()
                        if short_name and short_name in root_lower:
                            inferred_match = True
                            break

                    if inferred_match:
                        _add_level_if_unique(candidate_level_name, "main-root inference")

        if not matched_levels:
            for level in self.level_assets:
                _add_level_if_unique(level, "fallback all-levels list")

        final_levels = []
        for level in matched_levels:
            if self._is_main_wrapper_level(level):
                self._debug_log("Removed wrapper level '{}' from final results".format(level))
                continue
            final_levels.append(level)
        self._debug_log("Final model levels: {}".format(final_levels))
        return final_levels

    @staticmethod
    def _get_desc_field(desc_obj, field_name, default=None):
        if hasattr(desc_obj, field_name):
            return getattr(desc_obj, field_name)
        snake_name = []
        for idx, ch in enumerate(field_name):
            if ch.isupper() and idx > 0:
                snake_name.append("_")
            snake_name.append(ch.lower())
        snake_name = "".join(snake_name)
        if hasattr(desc_obj, snake_name):
            return getattr(desc_obj, snake_name)
        if field_name.startswith("b") and len(field_name) > 1 and field_name[1].isupper():
            alt_name = snake_name[2:] if snake_name.startswith("b_") else snake_name
            if hasattr(desc_obj, alt_name):
                return getattr(desc_obj, alt_name)
        return default

    # ------------------------------------------------------------------
    def _build_ui(self):
        outer_layout = QVBoxLayout(self)
        outer_layout.setContentsMargins(8, 8, 8, 8)
        outer_layout.setSpacing(8)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll_contents = QWidget()
        scroll_layout = QVBoxLayout(scroll_contents)
        scroll_layout.setContentsMargins(0, 0, 0, 0)
        scroll_layout.setSpacing(8)
        scroll_layout.addWidget(self._build_levels_section())
        scroll_layout.addWidget(self._build_modules_section())
        scroll_layout.addStretch(1)
        scroll.setWidget(scroll_contents)

        outer_layout.addWidget(scroll, stretch=1)
        outer_layout.addLayout(self._build_bottom_bar())
        outer_layout.addLayout(self._build_progress_section())

    # ------------------------------------------------------------------
    def _build_levels_section(self):
        group = QGroupBox("Levels to auto-populate from")
        layout = QVBoxLayout(group)
        layout.setContentsMargins(8, 16, 8, 8)
        layout.setSpacing(6)

        top_bar = QHBoxLayout()
        top_bar.setContentsMargins(0, 0, 0, 0)
        top_bar.addStretch(1)

        self.open_packaged_levels_button = QPushButton("Open Packaged Levels List")
        self.open_packaged_levels_button.setEnabled(_is_packaged_levels_list_available())
        self.open_packaged_levels_button.clicked.connect(
            lambda: _open_packaged_levels_list(self)
        )
        top_bar.addWidget(self.open_packaged_levels_button)
        layout.addLayout(top_bar)

        self.levels_list = QListWidget()
        self.levels_list.setSelectionMode(QListWidget.NoSelection)
        self.levels_list.setMinimumHeight(150)
        for level in self.level_assets:
            item = QListWidgetItem(level)
            item.setFlags(item.flags() | Qt.ItemIsUserCheckable)
            item.setCheckState(
                Qt.Checked if level in self.default_ticked_levels else Qt.Unchecked
            )
            self.levels_list.addItem(item)

        layout.addWidget(self.levels_list)
        return group

    # ------------------------------------------------------------------
    def _build_modules_section(self):
        outer_group = QGroupBox("Module Auto-Populate Settings")
        outer_layout = QVBoxLayout(outer_group)
        outer_layout.setContentsMargins(8, 16, 8, 8)
        outer_layout.setSpacing(4)

        top_bar = QHBoxLayout()
        top_bar.setContentsMargins(0, 0, 0, 0)
        top_bar.addStretch(1)

        select_all_button = QPushButton("Select all")
        select_none_button = QPushButton("Select none")
        select_all_button.clicked.connect(
            lambda checked=False: self._set_all_module_options_check_state(True)
        )
        select_none_button.clicked.connect(
            lambda checked=False: self._set_all_module_options_check_state(False)
        )
        top_bar.addWidget(select_all_button)
        top_bar.addWidget(select_none_button)
        outer_layout.addLayout(top_bar)

        if not self.module_descs:
            empty_label = QLabel("No plugins implementing IZLOmniStream_SchemaAutoPopulate are registered.")
            empty_label.setWordWrap(True)
            outer_layout.addWidget(empty_label)
        else:
            for desc in self.module_descs:
                plugin_name = self._to_py_string(self._get_desc_field(desc, "PluginName", ""))
                option_names = [self._to_py_string(opt) for opt in list(self._get_desc_field(desc, "OptionNames", []) or [])]
                advanced_descs = list(self._get_desc_field(desc, "AdvancedOptionDescs", []) or [])
                outer_layout.addWidget(self._build_module_group(plugin_name, option_names, advanced_descs))

        outer_layout.addStretch(1)
        return outer_group

    # ------------------------------------------------------------------
    def _build_module_group(self, plugin_name, option_names, advanced_descs):
        title = "{} - {}".format(plugin_name, ", ".join(option_names))
        group = QGroupBox(title)
        group.setCheckable(False)
        group.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Maximum)
        layout = QVBoxLayout(group)
        layout.setContentsMargins(12, 16, 12, 8)
        layout.setSpacing(4)

        advanced_desc_by_option = {}
        for advanced_desc in advanced_descs:
            option_name = self._to_py_string(self._get_desc_field(advanced_desc, "OptionName", ""))
            if option_name:
                advanced_desc_by_option[option_name] = advanced_desc

        for option in option_names:
            checkbox = QCheckBox(option)
            checkbox.setChecked(option in self.currently_selected_options)
            self.module_option_checkboxes.append(checkbox)
            layout.addWidget(checkbox)
            self.option_checkbox_map[option] = checkbox

            advanced_desc = advanced_desc_by_option.get(option)
            if advanced_desc:
                advanced_group = self._build_option_advanced_group(option, advanced_desc)
                advanced_group.setVisible(checkbox.isChecked())
                checkbox.toggled.connect(lambda checked, grp=advanced_group: grp.setVisible(checked))
                layout.addWidget(advanced_group)
                self.option_advanced_groups[option] = advanced_group

        return group

    # ------------------------------------------------------------------
    def _build_option_advanced_group(self, option_name, advanced_desc):
        foldout_title = self._to_py_string(
            self._get_desc_field(advanced_desc, "FoldoutTitle", "")
        ) or "{} - Advanced Settings".format(option_name)

        group = QGroupBox(foldout_title)
        group_layout = QVBoxLayout(group)
        group_layout.setContentsMargins(12, 16, 12, 8)
        group_layout.setSpacing(6)

        controls = {"checkboxes": {}, "level_selectors": {}}

        for checkbox_desc in list(self._get_desc_field(advanced_desc, "Checkboxes", []) or []):
            checkbox_id = self._to_py_string(self._get_desc_field(checkbox_desc, "Id", ""))
            checkbox_label = self._to_py_string(self._get_desc_field(checkbox_desc, "Label", checkbox_id))
            default_checked = bool(self._get_desc_field(checkbox_desc, "bDefaultChecked", False))
            if not checkbox_id:
                continue

            checkbox_widget = QCheckBox(checkbox_label)
            checkbox_widget.setChecked(default_checked)
            group_layout.addWidget(checkbox_widget)
            controls["checkboxes"][checkbox_id] = checkbox_widget

        for selector_desc in list(self._get_desc_field(advanced_desc, "LevelSelectors", []) or []):
            selector_id = self._to_py_string(self._get_desc_field(selector_desc, "Id", ""))
            selector_label = self._to_py_string(self._get_desc_field(selector_desc, "Label", selector_id))
            actor_class_path = self._to_py_string(self._get_desc_field(selector_desc, "RequiredActorClassPath", ""))
            component_class_path = self._to_py_string(self._get_desc_field(selector_desc, "RequiredActorComponentClassPath", ""))
            if not selector_id:
                continue

            selector_container = QGroupBox()
            selector_layout = QVBoxLayout(selector_container)
            selector_layout.setContentsMargins(8, 16, 8, 8)
            selector_layout.setSpacing(4)

            selector_header_row = QHBoxLayout()
            selector_header_row.setContentsMargins(0, 0, 0, 0)
            selector_header_row.addWidget(QLabel(selector_label))
            selector_header_row.addStretch(1)

            select_all_button = QPushButton("Select all")
            select_none_button = QPushButton("Select none")
            selector_header_row.addWidget(select_all_button)
            selector_header_row.addWidget(select_none_button)
            selector_layout.addLayout(selector_header_row)

            selector_list = QListWidget()
            selector_list.setSelectionMode(QListWidget.NoSelection)
            if self._is_model_config_selector(option_name, selector_id):
                selector_entries = list(self.discovered_dime_model_entries)
                self._debug_log(
                    "Using dimebake model discovery for selector '{}': {} entries".format(
                        selector_id,
                        len(selector_entries)
                    )
                )
                for entry in selector_entries:
                    item = QListWidgetItem(entry["display_name"])
                    item.setFlags(item.flags() | Qt.ItemIsUserCheckable)
                    item.setCheckState(Qt.Checked)
                    item.setData(Qt.UserRole, entry["selector_value"])
                    selector_list.addItem(item)
            else:
                selector_values = self._get_matching_levels_for_selector(actor_class_path, component_class_path)
                for selector_value in selector_values:
                    item = QListWidgetItem(selector_value)
                    item.setFlags(item.flags() | Qt.ItemIsUserCheckable)
                    item.setCheckState(Qt.Checked)
                    item.setData(Qt.UserRole, selector_value)
                    selector_list.addItem(item)

            select_all_button.clicked.connect(
                lambda checked=False, lst=selector_list: self._set_all_items_check_state(lst, Qt.Checked)
            )
            select_none_button.clicked.connect(
                lambda checked=False, lst=selector_list: self._set_all_items_check_state(lst, Qt.Unchecked)
            )

            selector_layout.addWidget(selector_list)
            group_layout.addWidget(selector_container)
            controls["level_selectors"][selector_id] = selector_list

        self.option_advanced_controls[option_name] = controls
        return group

    # ------------------------------------------------------------------
    def _build_bottom_bar(self):
        bar = QHBoxLayout()
        bar.setContentsMargins(0, 0, 0, 0)

        self.auto_populate_button = QPushButton("Auto Populate")
        self.auto_populate_button.clicked.connect(self._on_auto_populate_clicked)

        self.additive_checkbox = QCheckBox("Additive mode")
        self.additive_checkbox.setToolTip(
            "when selected, auto populate will not remove any of the existing keys/values in the current schema"
        )
        self.additive_checkbox.setChecked(True)

        bar.addWidget(self.auto_populate_button)
        bar.addWidget(self.additive_checkbox)
        bar.addStretch(1)
        return bar

    # ------------------------------------------------------------------
    def _build_progress_section(self):
        section = QVBoxLayout()
        section.setContentsMargins(0, 0, 0, 0)
        section.setSpacing(4)

        self.progress_label = QLabel("0% - Idle")
        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)

        section.addWidget(self.progress_label)
        section.addWidget(self.progress_bar)
        return section

    # ------------------------------------------------------------------
    def _set_progress(self, percent, stage_text):
        value = max(0, min(100, int(percent)))
        self.progress_bar.setValue(value)
        self.progress_label.setText("{}% - {}".format(value, stage_text))
        QApplication.processEvents()

    def _set_running_ui(self, is_running):
        self.auto_populate_button.setEnabled(not is_running)
        QApplication.processEvents()

    # ------------------------------------------------------------------
    def _gather_selected_options(self):
        return [cb.text() for cb in self.module_option_checkboxes if cb.isChecked()]

    def _persist_selected_options(self):
        _persist_selected_auto_populate_options(self._gather_selected_options())

    def _snapshot_current_settings(self):
        self._saved_selected_options = set(self._gather_selected_options())
        self._saved_selected_levels = set(self._gather_selected_levels())
        self._saved_additive_mode = self.additive_checkbox.isChecked()
        self._saved_advanced_settings_json = json.dumps(self._gather_advanced_settings(), sort_keys=True)

    def _has_unsaved_settings_changes(self):
        if set(self._gather_selected_options()) != self._saved_selected_options:
            return True
        if set(self._gather_selected_levels()) != self._saved_selected_levels:
            return True
        if self.additive_checkbox.isChecked() != self._saved_additive_mode:
            return True
        if json.dumps(self._gather_advanced_settings(), sort_keys=True) != self._saved_advanced_settings_json:
            return True
        return False

    def _close_after_successful_auto_populate(self):
        self._persist_selected_options()
        self._snapshot_current_settings()
        self._suppress_close_save_prompt = True
        self.close()

    def closeEvent(self, event):
        if not self._suppress_close_save_prompt and self._has_unsaved_settings_changes():
            response = QMessageBox.question(
                self,
                "Save changes?",
                "You have unsaved auto-populate settings.\n\nSave before closing?",
                QMessageBox.Yes | QMessageBox.No | QMessageBox.Cancel,
                QMessageBox.Yes
            )
            if response == QMessageBox.Cancel:
                event.ignore()
                return
            if response == QMessageBox.Yes:
                self._persist_selected_options()
                self._snapshot_current_settings()

        super(AdvancedAutoPopulateWindow, self).closeEvent(event)

    def _gather_selected_levels(self):
        selected = []
        for row in range(self.levels_list.count()):
            item = self.levels_list.item(row)
            if item.checkState() == Qt.Checked:
                selected.append(item.text())
        return selected

    def _gather_advanced_settings(self):
        payload = {}
        for option_name, controls in self.option_advanced_controls.items():
            if option_name not in self.option_checkbox_map or not self.option_checkbox_map[option_name].isChecked():
                continue

            option_payload = {}
            checkbox_payload = {}
            for checkbox_id, checkbox_widget in controls.get("checkboxes", {}).items():
                checkbox_payload[checkbox_id] = checkbox_widget.isChecked()
            if checkbox_payload:
                option_payload["checkboxes"] = checkbox_payload

            level_selection_payload = {}
            actor_selection_payload = {}
            for selector_id, selector_list in controls.get("level_selectors", {}).items():
                selected_levels = []
                selected_actor_labels = []
                for row in range(selector_list.count()):
                    item = selector_list.item(row)
                    if item.checkState() == Qt.Checked:
                        level_package = item.data(Qt.UserRole)
                        level_value = self._to_py_string(level_package if level_package else item.text())
                        if level_value:
                            selected_levels.append(level_value)
                        actor_label = self._to_py_string(item.text())
                        if actor_label:
                            selected_actor_labels.append(actor_label)
                level_selection_payload[selector_id] = selected_levels
                actor_selection_payload[selector_id] = selected_actor_labels
            if level_selection_payload:
                option_payload["levelSelections"] = level_selection_payload
            if actor_selection_payload:
                option_payload["actorSelections"] = actor_selection_payload

            if option_payload:
                payload[option_name] = option_payload

        return payload

    @staticmethod
    def _gather_option_specific_levels(advanced_settings_payload):
        levels_by_option = {}
        for option_name, option_payload in advanced_settings_payload.items():
            level_selections = option_payload.get("levelSelections", {})
            combined_levels = []
            for selected_levels in level_selections.values():
                for level_name in selected_levels:
                    if level_name not in combined_levels:
                        combined_levels.append(level_name)
            levels_by_option[option_name] = combined_levels
        return levels_by_option

    @staticmethod
    def _build_level_work_items(selected_options, selected_levels, option_specific_levels):
        level_to_options = {}
        editor_world_options = []

        for option in selected_options:
            option_levels = option_specific_levels.get(option, [])
            if option_levels:
                for level_name in option_levels:
                    if level_name not in level_to_options:
                        level_to_options[level_name] = []
                    if option not in level_to_options[level_name]:
                        level_to_options[level_name].append(option)
            elif selected_levels:
                for level_name in selected_levels:
                    if level_name not in level_to_options:
                        level_to_options[level_name] = []
                    if option not in level_to_options[level_name]:
                        level_to_options[level_name].append(option)
            else:
                editor_world_options.append(option)

        work_items = [(level_name, level_to_options[level_name]) for level_name in level_to_options]
        if editor_world_options:
            work_items.append((None, editor_world_options))
        return work_items

    # ------------------------------------------------------------------
    def _on_auto_populate_clicked(self):
        if not unreal.ZLOmniStreamAutoPopulateLibrary.is_schema_editor_open():
            QMessageBox.warning(
                self,
                "Schemas Editor not open",
                "The OmniStream Schemas Editor (V2) is not open.\n"
                "Open the editor before running advanced auto-populate."
            )
            return

        selected_options = self._gather_selected_options()
        if not selected_options:
            QMessageBox.information(
                self,
                "No options selected",
                "Tick at least one option in 'Module Auto-Populate Settings' before running auto populate."
            )
            return

        selected_levels = self._gather_selected_levels()
        advanced_settings_payload = self._gather_advanced_settings()
        advanced_settings_json = json.dumps(advanced_settings_payload)
        option_specific_levels = self._gather_option_specific_levels(advanced_settings_payload)
        additive = self.additive_checkbox.isChecked()
        loaded_schema_path = unreal.ZLOmniStreamAutoPopulateLibrary.get_loaded_schema_path()
        baseline_path = _resolve_baseline_path(str(loaded_schema_path) if loaded_schema_path else "")

        # Run in level-first steps so each level is resolved once with all relevant options.
        work_items = self._build_level_work_items(selected_options, selected_levels, option_specific_levels)
        self._debug_log("Selected options before run: {}".format(selected_options))
        self._debug_log("Generated work items: {}".format(work_items))

        total_steps = len(work_items)
        self._set_running_ui(True)
        self._set_progress(0, "Preparing auto populate")

        if baseline_path:
            unreal.ZLOmniStreamAutoPopulateLibrary.clear_auto_populate_preview()

            proposed_schema_text = ""
            first_step = True
            for step_index, (level, options_for_step) in enumerate(work_items, start=1):
                level_label = level if level else "current editor world"
                options_label = ", ".join(options_for_step)
                before_percent = ((step_index - 1) / total_steps) * 100.0 if total_steps > 0 else 0.0
                self._set_progress(before_percent, "Generating '{}' from level '{}'".format(options_label, level_label))

                # In non-additive mode, clear once on first step, then merge thereafter.
                step_additive = additive or not first_step
                first_step = False

                level_arg = [level] if level else []
                ok, proposed_schema_text = _run_preview_step(options_for_step, level_arg, step_additive, advanced_settings_json)
                if not ok:
                    self._set_running_ui(False)
                    self._set_progress(before_percent, "Failed at '{}' from '{}'".format(options_label, level_label))
                    unreal.ZLOmniStreamAutoPopulateLibrary.clear_auto_populate_preview()
                    QMessageBox.warning(
                        self,
                        "Auto-Populate preview failed",
                        "Auto-populate preview could not be generated."
                    )
                    return

                after_percent = (step_index / total_steps) * 100.0 if total_steps > 0 else 100.0
                self._set_progress(after_percent, "Generated '{}' from level '{}'".format(options_label, level_label))

            try:
                with open(baseline_path, "r", encoding="utf-8") as schema_file:
                    baseline_text = schema_file.read()
            except Exception as read_error:
                self._set_running_ui(False)
                unreal.ZLOmniStreamAutoPopulateLibrary.clear_auto_populate_preview()
                QMessageBox.warning(
                    self,
                    "Baseline read failed",
                    "Could not read baseline schema file:\n{}\n\n{}".format(baseline_path, read_error)
                )
                return

            self._set_progress(100, "Preview complete - awaiting confirmation")
            normalized_baseline_text = _canonicalize_schema_for_diff(baseline_text)
            normalized_proposed_text = _canonicalize_schema_for_diff(proposed_schema_text)
            dialog = DiffConfirmDialog(
                baseline_path,
                normalized_baseline_text,
                normalized_proposed_text,
                self
            )
            dialog_exec = getattr(dialog, "exec", None) or getattr(dialog, "exec_", None)
            if dialog_exec and dialog_exec() == QDialog.Accepted:
                applied = unreal.ZLOmniStreamAutoPopulateLibrary.apply_auto_populate_preview()
                self._set_running_ui(False)
                if not applied:
                    QMessageBox.warning(
                        self,
                        "Apply failed",
                        "Auto-populate changes could not be applied or saved to the schema editor."
                    )
                    return

                QMessageBox.information(
                    self,
                    "Auto-Populate applied",
                    "Auto-populate changes were applied and the schema was saved."
                )
                self._set_progress(100, "Auto populate complete")
                self._close_after_successful_auto_populate()
                return

            unreal.ZLOmniStreamAutoPopulateLibrary.clear_auto_populate_preview()
            self._set_running_ui(False)
            self._set_progress(0, "Preview discarded")
            return

        first_step = True
        for step_index, (level, options_for_step) in enumerate(work_items, start=1):
            level_label = level if level else "current editor world"
            options_label = ", ".join(options_for_step)
            before_percent = ((step_index - 1) / total_steps) * 100.0 if total_steps > 0 else 0.0
            self._set_progress(before_percent, "Generating '{}' from level '{}'".format(options_label, level_label))

            # In non-additive mode, clear once on first step, then merge thereafter.
            step_additive = additive or not first_step
            first_step = False

            level_arg = [level] if level else []
            ok = _run_apply_step(options_for_step, level_arg, step_additive, advanced_settings_json)
            if not ok:
                self._set_running_ui(False)
                self._set_progress(before_percent, "Failed at '{}' from '{}'".format(options_label, level_label))
                QMessageBox.warning(
                    self,
                    "Auto-Populate failed",
                    "Auto-populate could not be applied to the schema editor."
                )
                return

            after_percent = (step_index / total_steps) * 100.0 if total_steps > 0 else 100.0
            self._set_progress(after_percent, "Generated '{}' from level '{}'".format(options_label, level_label))

        self._set_running_ui(False)
        self._set_progress(100, "Auto populate complete")
        QMessageBox.information(
            self,
            "Auto-Populate applied",
            "Auto-populate changes were applied and the schema was saved."
        )
        self._close_after_successful_auto_populate()


def open_window():
    AdvancedAutoPopulateWindow.window = qt_window_styling.create_and_show_window(
        AdvancedAutoPopulateWindow,
        "AdvancedAutoPopulate",
        "Advanced Auto-Populate",
        make_taskbar_visible=True,
        parent_to_slate=False
    )
    AdvancedAutoPopulateWindow.window.resize(550,850)


if __name__ == "__main__":
    open_window()
