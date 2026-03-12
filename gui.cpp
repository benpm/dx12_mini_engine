#include <gui.hpp>
#include <gfx.hpp>
#include <app.hpp>
#include <scenes.hpp>
#include <algorithm>
#include <array>
#include <model.hpp>
#include <util.hpp>
#include <stb_image.h>

// ImGui custom assert handler that integrates with our logging system
void ImGuiAssertHandler(const char* expr, const char* file, int line)
{
    $error("ImGui assertion failed: {} at {}:{}", expr, file, line);
}

// =============================================================================
// EditorAction Implementation
// =============================================================================

std::string_view EditorAction::keyName(int key)
{
    constexpr static std::string_view chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const static std::unordered_map<int, std::string_view> keys = {
        { GLFW_KEY_ESCAPE, "Escape" },   { GLFW_KEY_SPACE, "Space" },
        { GLFW_KEY_DELETE, "Delete" },   { GLFW_KEY_BACKSPACE, "Backspace" },
        { GLFW_KEY_ENTER, "Enter" },     { GLFW_KEY_TAB, "Tab" },
        { GLFW_KEY_F1, "F1" },           { GLFW_KEY_F2, "F2" },
        { GLFW_KEY_F3, "F3" },           { GLFW_KEY_F4, "F4" },
        { GLFW_KEY_F5, "F5" },           { GLFW_KEY_F6, "F6" },
        { GLFW_KEY_F7, "F7" },           { GLFW_KEY_F8, "F8" },
        { GLFW_KEY_F9, "F9" },           { GLFW_KEY_F10, "F10" },
        { GLFW_KEY_F11, "F11" },         { GLFW_KEY_F12, "F12" },
        { GLFW_KEY_LEFT, "Left Arrow" }, { GLFW_KEY_RIGHT, "Right Arrow" },
        { GLFW_KEY_UP, "Up Arrow" },     { GLFW_KEY_DOWN, "Down Arrow" },
        { GLFW_KEY_HOME, "Home" },       { GLFW_KEY_END, "End" },
        { GLFW_KEY_PAGE_UP, "Page Up" }, { GLFW_KEY_PAGE_DOWN, "Page Down" },
        { GLFW_KEY_INSERT, "Insert" },

    };
    constexpr static std::string_view unkownCharSymbol = "??";
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        return chars.substr(static_cast<size_t>(key - GLFW_KEY_A), 1);
    } else {
        return keys.contains(key) ? keys.at(key) : unkownCharSymbol;
    }
}

std::string EditorAction::tooltip() const
{
    std::string tip = name;
    if (key != 0) {
        std::string keyStr;
        if (requiresCtrl) {
            keyStr += "Ctrl+";
        }
        if (requiresShift) {
            keyStr += "Shift+";
        }
        if (requiresAlt) {
            keyStr += "Alt+";
        }
        keyStr += keyName(key);
        if (!keyStr.empty()) {
            tip += " (" + keyStr + ")";
        }
    }
    return tip;
}

bool EditorAction::matchesKeyCombo(int pressedKey, bool ctrl, bool shift, bool alt) const
{
    if (key == 0 || key != pressedKey) {
        return false;
    }
    return (requiresCtrl == ctrl) && (requiresShift == shift) && (requiresAlt == alt);
}

// =============================================================================
// EditorActions Registry Implementation
// =============================================================================

std::map<std::string, EditorAction> EditorActions::actions;
std::vector<EditorAction> EditorActions::actionsVector;
bool EditorActions::initialized = false;

void EditorActions::init()
{
    if (initialized) {
        return;
    }
    initialized = true;

    ///LABEL: actions definitions
    // Initialize as a temporary vector first, then convert to map
    std::vector<EditorAction> tempActions = {
        // Select/Idle mode (also executes slice if slice plane is active)
        { .id = "select",
          .name = "Select",
          .icon = "mouse-pointer-2.png",
          .key = GLFW_KEY_ESCAPE,
          .requiresSurface = false,
          .showInToolbar = true,
          .isActive =
              [](const App& app) {
                  return app.state() == App::State::idle &&
                         !(app.gizSlicePlane && app.gizSlicePlane->isRender);
              },
          .execute =
              [](App& app) {
                  // When slicing, Escape cancels slicing (hide slice gizmos) without clearing selection.
                  if (app.state() == App::State::slicing ||
                      (app.gizSlicePlane && app.gizSlicePlane->isRender)) {
                      app.state(App::State::idle);
                      return;
                  }
                  app.state(App::State::idle);
                  // Deselect all selected geometry
                  if (app.selectedSurface) {
                      app.selectedSurface->clearSelection();
                  }
              } },
        // Execute slice with Enter key
        { .id = "execute_slice",
          .name = "Execute Slice",
          .icon = "slice.png",
          .key = GLFW_KEY_ENTER,
          .requiresSurface = false,
          .showInToolbar = false,
          .isActive =
              [](const App& app) {
                  return false;  // Never shown as active
              },
          .execute =
              [](App& app) {
                  if (app.gizSlicePlane && app.gizSlicePlane->isRender) {
                      app.executeSlice();
                  }
              } },
        // Sculpt mode
        { .id = "sculpt",
          .name = "Sculpt",
          .icon = "sculpt.png",
          .key = 0,  // No key binding yet
          .requiresSurface = true,
          .showInToolbar = true,
          .isActive = [](const App& app) { return app.state() == App::State::sculpting; },
          .execute =
              [](App& app) {
                  if (app.state() == App::State::sculpting) {
                      app.state(App::State::idle);
                  } else {
                      app.state(App::State::sculpting);
                  }
              } },
        // Carve mode
        { .id = "carve",
          .name = "Carve",
          .icon = "carve.png",
          .key = GLFW_KEY_C,
          .requiresSurface = true,
          .showInToolbar = true,
          .isActive = [](const App& app) { return app.state() == App::State::carving; },
          .execute =
              [](App& app) {
                  if (app.state() == App::State::carving) {
                      app.state(App::State::idle);
                  } else {
                      app.state(App::State::carving);
                  }
                  if (app.selectedSurface) {
                      app.selectedSurface->clearSelection();
                  }
              } },
        // Slice plane
        { .id = "slice",
          .name = "Slice Plane",
          .icon = "slice.png",
          .key = 0,  // No key binding
          .requiresSurface = true,
          .showInToolbar = !Surface::g_MacroPolyMode,
          .isActive =
              [](const App& app) { return app.gizSlicePlane && app.gizSlicePlane->isRender; },
          .execute =
              [](App& app) {
                  if (Surface::g_MacroPolyMode) {
                      return;
                  }

                  if (app.gizSlicePlane->isRender) {
                      // Execute slice when button pressed again
                      app.executeSlice();
                  } else {
                      app.state(App::State::slicing);
                  }
              } },
        // Extrude
        { .id = "extrude",
          .name = "Extrude",
          .icon = "extrude.png",
          .key = GLFW_KEY_E,
          .requiresSurface = true,
          .showInToolbar = true,
          .isActive =
              [](const App& app) {
                  return false;  // Extrude is a one-shot action, never "active"
              },
          .execute =
              [](App& app) {
                  if (!app.selectedSurface) {
                      return;
                  }
                  app.moveDir = app.selectedSurface->avgNormal(app.selectedSurface->selected);
                  Vector3f centroid = app.selectedSurface->centroid(app.selectedSurface->selected);
                  app.lastEditOp = app.history.add<OpEditGeom>(app.tree, app.selectedSurface);
                  app.lastEditOp->storeBefore();
                  const bool ok = app.doAction([&](App& app) {
                      if (!app.selectedSurface || !app.moveDir.has_value()) {
                          return;
                      }
                      app.selectedSurface->extrude(*app.moveDir);
                  });
                  if (!ok) {
                      // Drop the partially created undo op on failure.
                      app.doAction([](App& app) { app.history.undo(); });
                      app.lastEditOp.reset();
                      return;
                  }
                  // After extrude, show transform gizmo with local coordinates
                  app.useLocalCoordinates = true;
                  app.state(App::State::transform, app.gizTranslate);
                  app.onSelection();  // Update gizmo position
              } },
        // Add Point
        { .id = "add_point",
          .name = "Add Point",
          .icon = "add-point.png",
          .key = 0,  // No key binding
          .requiresSurface = true,
          .showInToolbar = true,
          .isActive = [](const App& app) { return app.state() == App::State::addingPoint; },
          .execute =
              [](App& app) {
                  if (app.state() == App::State::addingPoint) {
                      app.state(App::State::idle);
                  } else {
                      app.state(App::State::addingPoint);
                  }
              } },
        // Remesh
        { .id = "remesh",
          .name = "Remesh",
          .icon = "remesh.png",
          .key = GLFW_KEY_R,
          .requiresSurface = false,  // Allow remeshing all surfaces when none selected
          .showInToolbar = true,
          .isActive =
              [](const App& app) {
                  return false;  // Remesh is a one-shot action
              },
          .execute = [](App& app) { app.remeshSelection(); } },
        // Undo
        { .id = "undo",
          .name = "Undo",
          .icon = "undo.png",
          .key = GLFW_KEY_Z,
          .requiresCtrl = true,
          .requiresSurface = false,
          .showInToolbar = false,
          .isActive =
              [](const App& app) {
                  return false;  // Undo is a one-shot action
              },
          .execute =
              [](App& app) {
                  app.doAction([](App& app) {
                      app.history.undo();
                      app.deselectAll();
                  });
              } },
        // Redo
        { .id = "redo",
          .name = "Redo",
          .icon = "redo.png",
          .key = GLFW_KEY_Z,
          .requiresCtrl = true,
          .requiresShift = true,
          .requiresSurface = false,
          .showInToolbar = false,
          .isActive =
              [](const App& app) {
                  return false;  // Redo is a one-shot action
              },
          .execute =
              [](App& app) {
                  app.doAction([](App& app) {
                      app.history.redo();
                      app.deselectAll();
                  });
              } },
        // Delete selected meta-geometry
        { .id = "delete",
          .name = "Delete",
          .icon = "trash.png",
          .key = GLFW_KEY_DELETE,
          .requiresSurface = true,
          .showInToolbar = false,
          .isActive =
              [](const App& app) {
                  return false;  // Delete is a one-shot action
              },
          .execute =
              [](App& app) {
                  if (!app.selectedSurface) {
                      return;
                  }
                  const auto metaIDs = app.selectedSurface->selected;
                  const bool ok = app.doAction([&](App& app) {
                      Surface* s = app.selectedSurface.get();
                      if (!s) {
                          return;
                      }
                      for (uint64_t metaID : metaIDs) {
                          switch (index_type::tag(metaID)) {
                              case Surface::patch_idx_t::tagval:
                                  s->removePatch(metaID);
                                  break;
                              case Surface::border_idx_t::tagval:
                                  s->joinPatches(metaID);
                                  break;
                              case Surface::point_idx_t::tagval:
                                  if (s->getBorders(metaID).size() == 1 &&
                                      s->getPatches(metaID).size() == 1) {
                                      s->removePoint(metaID);
                                  } else {
                                      app.displayError(
                                          "Cannot remove point with multiple connections"
                                      );
                                  }
                                  break;
                          }
                      }
                  });
                  if (ok) {
                      app.deselectAll();
                  }
              } },
        // Duplicate surface (Ctrl+D)
        { .id = "duplicate",
          .name = "Duplicate",
          .icon = "copy.png",
          .key = GLFW_KEY_D,
          .requiresCtrl = true,
          .requiresSurface = true,
          .showInToolbar = false,
          .isActive =
              [](const App& app) {
                  return false;  // Duplicate is a one-shot action
              },
          .execute =
              [](App& app) {
                  if (!app.selectedSurface) {
                      return;
                  }
                  // Check if at least one patch is selected
                  bool selectionHasPatch = false;
                  for (const uint64_t id : app.selectedSurface->selected) {
                      if (Surface::patch_idx_t::valid(id)) {
                          selectionHasPatch = true;
                          break;
                      }
                  }

                  if (selectionHasPatch) {
                      // Wrap in doAction for proper error handling
                      app.doAction([](App& app) {
                          // Duplicate selected surface and select it
                          NodeWrapper<Surface> clone = app.tree.create<Surface>(
                              app.history, *app.selectedSurface.get(),
                              app.selectedSurface->expandSelection()
                          );
                          app.deselectAll();
                          app.selectedSurface = clone;
                          app.selectedSurface->clearSelection();
                          app.selectedSurface->selected = app.selectedSurface->allMetaGeom();
                          app.onSelection();
                      });
                  }
              } },
        // Save surface (Ctrl+S)
        { .id = "save",
          .name = "Save",
          .icon = "save.png",
          .key = GLFW_KEY_S,
          .requiresCtrl = true,
          .requiresSurface = true,
          .showInToolbar = false,
          .isActive =
              [](const App& app) {
                  return false;  // Save is a one-shot action
              },
          .execute =
              [](App& app) {
                  if (!app.selectedSurface) {
                      return;
                  }
                  app.doAction([](App& app) {
                      if (!app.selectedSurface) {
                          return;
                      }
                      app.selectedSurface->pack().write(opts::outdir / "surface.data");
                  });
              } },
        // Load surface (Ctrl+O)
        { .id = "load",
          .name = "Load",
          .icon = "folder-open.png",
          .key = GLFW_KEY_O,
          .requiresCtrl = true,
          .requiresSurface = false,
          .showInToolbar = false,
          .isActive =
              [](const App& app) {
                  return false;  // Load is a one-shot action
              },
          .execute =
              [](App& app) {
                  fs::path path = opts::outdir / "surface.data";
                  if (fs::exists(path)) {
                      $info("Reading surface from {}", path.string());
                      app.tree.create<Surface>(app.history, path);
                  } else {
                      $warn("File {} does not exist", path.string());
                  }
              } },
        // Transform mode (S key - cycle through gizmos)
        { .id = "transform_mode",
          .name = "Transform Mode",
          .icon = "rotate-3d.png",
          .key = GLFW_KEY_S,
          .requiresSurface = false,
          .showInToolbar = false,
          .isActive =
              [](const App& app) {
                  return false;  // This cycles through modes, no single active state
              },
          .execute =
              [](App& app) {
                  // Special case: while slicing, keep slicing active and just switch
                  // between plane translate/rotate gizmos.
                  if (app.state() == App::State::slicing && app.gizSlicePlane &&
                      app.gizSlicePlane->isRender) {
                      NodeWrapper<Gizmo> currentGizmo = app.curGizmo;
                      NodeWrapper<Gizmo> nextGizmo = currentGizmo;

                      if (currentGizmo == app.gizTransPlane) {
                          nextGizmo = app.gizRotPlane;
                      } else {
                          nextGizmo = app.gizTransPlane;
                      }

                      if (nextGizmo && currentGizmo && nextGizmo != currentGizmo) {
                          nextGizmo->transform = currentGizmo->transform;
                      }

                      if (app.gizTransPlane) {
                          app.gizTransPlane->isRender = (nextGizmo == app.gizTransPlane);
                      }
                      if (app.gizRotPlane) {
                          app.gizRotPlane->isRender = (nextGizmo == app.gizRotPlane);
                      }
                      app.curGizmo = nextGizmo;
                      return;
                  }

                  NodeWrapper<Gizmo> currentGizmo = app.curGizmo;
                  bool wasRendering = (app.state() == App::State::transform);
                  NodeWrapper<Gizmo> nextGizmo = currentGizmo;

                  if (app.gizSlicePlane->isRender) {
                      if (currentGizmo == app.gizTransPlane) {
                          nextGizmo = app.gizRotPlane;
                      } else if (currentGizmo == app.gizRotPlane) {
                          nextGizmo = app.gizTransPlane;
                      }
                  } else {
                      if (currentGizmo == app.gizScale) {
                          nextGizmo = app.gizTranslate;
                      } else if (currentGizmo == app.gizTranslate) {
                          nextGizmo = app.gizRotate;
                      } else if (currentGizmo == app.gizRotate) {
                          nextGizmo = app.gizScale;
                      }
                  }

                  if (nextGizmo && currentGizmo && nextGizmo != currentGizmo) {
                      nextGizmo->transform = currentGizmo->transform;
                  }

                  app.state(App::State::idle);
                  if (wasRendering) {
                      app.state(App::State::transform, nextGizmo);
                  } else {
                      app.state(app.state(), nextGizmo);
                  }
              } },
        // Toggle local/global coordinate mode for transform gizmos
        { .id = "toggle_local_coords",
          .name = "Local Coords",
          .icon = "move-3d.png",
          .key = 0,  // No key binding
          .requiresSurface = false,
          .showInToolbar = false,
          .isActive = [](const App& app) { return app.useLocalCoordinates; },
          .execute =
              [](App& app) {
                  app.useLocalCoordinates = !app.useLocalCoordinates;
                  // Update gizmo orientation if we have a selection
                  if (app.selectedSurface && !app.selectedSurface->selected.empty()) {
                      app.onSelection();
                  }
              } },
    };

    // Build the cached vector first to preserve insertion order (select tool first)
    actionsVector = std::move(tempActions);

    // Then build the map for ID-based lookup
    for (const auto& action : actionsVector) {
        actions[action.id] = action;
    }
}

const std::vector<EditorAction>& EditorActions::all()
{
    if (!initialized) {
        init();
    }
    return actionsVector;
}

const EditorAction* EditorActions::find(const std::string& id)
{
    if (!initialized) {
        init();
    }
    auto it = actions.find(id);
    if (it != actions.end()) {
        return &it->second;
    }
    return nullptr;
}

bool EditorActions::execute(const std::string& id, App& app)
{
    const EditorAction* action = find(id);
    if (!action) {
        return false;
    }
    if (action->requiresSurface && !app.selectedSurface) {
        return false;
    }
    action->execute(app);
    return true;
}

const EditorAction* EditorActions::findByKeyCombo(int key, bool ctrl, bool shift, bool alt)
{
    if (!initialized) {
        init();
    }
    if (key == 0) {
        return nullptr;
    }

    for (const auto& [id, action] : actions) {
        if (action.matchesKeyCombo(key, ctrl, shift, alt)) {
            return &action;
        }
    }
    return nullptr;
}

bool EditorActions::handleKeyPress(App& app, int key)
{
    const EditorAction* action =
        findByKeyCombo(key, app.keyboard.ctrl, app.keyboard.shift, app.keyboard.alt);
    if (!action) {
        return false;
    }

    // Check if action requires a surface and we have one
    if (action->requiresSurface && !app.selectedSurface) {
        return false;
    }

    action->execute(app);
    return true;
}

void styleColorsDracula()
{
    auto& colors = ImGui::GetStyle().Colors;
    (void)colors;
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.42f, 0.47f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.01f, 0.05f, 0.07f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    colors[ImGuiCol_Border] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.20f, 0.28f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.09f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.12f, 0.14f, 0.65f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.39f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.18f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.09f, 0.21f, 0.31f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.37f, 0.61f, 1.00f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.29f, 0.55f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
    colors[ImGuiCol_Tab] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

    auto& style = ImGui::GetStyle();
    (void)style;
    style.TabRounding = 4;
    style.ScrollbarRounding = 4;
    style.WindowRounding = 7;
    style.GrabRounding = 3;
    style.FrameRounding = 3;
    style.PopupRounding = 4;
    style.ChildRounding = 4;
}

float GUI::getDPIScaling(GLFWwindow* window)
{
    // Get DPI scaling from monitor
    float xscale, yscale;
    glfwGetMonitorContentScale(glfwGetPrimaryMonitor(), &xscale, &yscale);
    float dpiScale = std::max(xscale, yscale);
    $debug("DPI scale factor: {}", dpiScale);

    // Apply scaling to ImGui
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(dpiScale);
    return dpiScale;
}

bool GUI::initImGUI(GLFWwindow* window)
{
    // Initialize ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable keyboard navigation

    // Enable error recovery with debug logging
    io.ConfigErrorRecovery = true;
    io.ConfigErrorRecoveryEnableAssert = true;  // Enable asserts on errors
    io.ConfigErrorRecoveryEnableDebugLog = true;  // Enable debug log output
    io.ConfigErrorRecoveryEnableTooltip = true;  // Show error tooltips

    // Initialize ImGui for GLFW and OpenGL3
    bool initGLFW = ImGui_ImplGlfw_InitForOpenGL(window, true);
    $assert(initGLFW, "failed to initialize ImGui for GLFW");
    bool initGL = ImGui_ImplOpenGL3_Init("#version 330");
    $assert(initGL, "failed to initialize ImGui for OpenGL");
    return initGLFW && initGL;
}

GUI::GUI(GLFWwindow* window)
{
    const fs::path defaultFontPath = (opts::resources / "fonts" / "Roboto-Medium.ttf").make_preferred();
    const fs::path monospaceFontPath = (opts::resources / "fonts" / "RobotoMono-Medium.ttf").make_preferred();

    this->initImGUI(window);

    ImGuiIO& io = ImGui::GetIO();
    this->dpiScale = getDPIScaling(window);
    io.FontGlobalScale = dpiScale * 1.5f;  // Make fonts 1.5x larger

    // Load font with appropriate size for DPI
    ImFontConfig fontConfig;
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 2;

    $debug("Loading fonts: {} and {}", defaultFontPath.string(), monospaceFontPath.string());

    float fontSize = 13.0f * dpiScale;
    this->defaultFont = io.Fonts->AddFontFromFileTTF(
        defaultFontPath.string().c_str(), fontSize, &fontConfig
    );
    this->monospaceFont = io.Fonts->AddFontFromFileTTF(
        monospaceFontPath.string().c_str(), fontSize, &fontConfig
    );

    // If the font fails to load, fall back to default
    if (io.Fonts->Fonts.empty()) {
        $warn("failed to load {}", defaultFontPath.string());
        io.Fonts->AddFontDefault();
    }

    // Apply custom styling
    styleColorsDracula();
}

GUI::~GUI()
{
    // Clear cached textures before shutting down
    clearImageCache();

    // Shutdown ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void GUI::beginFrame() const
{
    // Start ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void GUI::endFrame() const
{
    // Render ImGui frame
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

gfx::Texture* GUI::loadImageTexture(const std::string& imagePath)
{
    // Check if image is already cached
    auto it = imageCache.find(imagePath);
    if (it != imageCache.end()) {
        return &it->second;
    }

    // Load image using stb_image
    int width, height, channels;
    unsigned char* data = stbi_load(imagePath.c_str(), &width, &height, &channels, 4);
    $assert(data, "failed to load: {}", imagePath.c_str());

    const GLenum format = channels == 4 ? GL_RGBA : GL_RGB;

    // Create texture configuration
    gfx::TextureConfig config = {
        .target = GL_TEXTURE_2D,
        .format = format,
        .internalFormat = GL_RGBA,
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .texUnit = GL_TEXTURE0,  // Will be bound manually for ImGui
        .storageType = GL_UNSIGNED_BYTE,
        .filter = GL_LINEAR,
        .data = data,
    };

    // Create and cache the texture
    auto result = imageCache.emplace(imagePath, gfx::Texture(config));
    return &result.first->second;
}

bool GUI::imageButton(const std::string& imagePath, float size, const char* tooltip)
{
    gfx::Texture* texture = loadImageTexture(imagePath);
    if (!texture) {
        // Fallback to regular button if image loading fails
        bool pressed = ImGui::Button("?", ImVec2(size, size));
        if (tooltip && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Failed to load: %s", imagePath.c_str());
        }
        return pressed;
    }

    // Create square button with image
    ImTextureID textureID = (ImTextureID)(intptr_t)texture->id;
    ImTextureRef ref(textureID);
    bool pressed = ImGui::ImageButton(imagePath.c_str(), ref, ImVec2(size, size));

    // Add tooltip if provided
    if (tooltip && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }

    return pressed;
}

void GUI::clearImageCache()
{
    // Delete all cached textures
    for (auto& pair : imageCache) {
        if (pair.second.id != GL_INVALID_INDEX) {
            glDeleteTextures(1, &pair.second.id);
        }
    }
    imageCache.clear();
}

// =============================================================================
// GUI Functions that operate on App state
// =============================================================================

namespace gui
{
    // Helper function to convert App::State to a display string
    static const char* stateToString(App::State state)
    {
        switch (state) {
            case App::State::idle:
                return "Idle";
            case App::State::sculpting:
                return "Sculpting";
            case App::State::carving:
                return "Carving";
            case App::State::slicing:
                return "Slicing";
            case App::State::addingPoint:
                return "Adding Point";
            case App::State::dragging:
                return "Dragging";
            case App::State::moving_with_mouse:
                return "Moving";
            case App::State::waiting_for_weights:
                return "Processing Weights";
            case App::State::debugging:
                return "Debugging";
            case App::State::transform:
                return "Transform";
            default:
                return "Unknown";
        }
    }

    // Helper to create a tool button with optional active state highlighting
    static bool toolButton(
        GUI& gui,
        const fs::path& iconPath,
        const char* tooltip,
        bool isActive,
        bool isDisabled,
        float size = 32.0f
    )
    {
        // If active, push highlighted button color with yellowish color
        // matching the selection color from resources/shaders/common/metageom.glsl
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.8f, 0.4f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.85f, 0.5f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.7f, 0.35f, 1.0f));
        } else if (isDisabled) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        }

        fs::path icoPath;
        if (fs::exists(iconPath)) {
            icoPath = { iconPath };
        } else {
            icoPath = opts::resources / "icons" / "box.png";
        }

        bool pressed = gui.imageButton(icoPath.string(), size, tooltip);

        if (isActive) {
            ImGui::PopStyleColor(3);
        } else if (isDisabled) {
            ImGui::PopStyleColor(1);
        }

        return pressed;
    }

    void guiToolbar(App& app, float menuBarHeight)
    {
        // Ensure actions are initialized
        const auto& actions = EditorActions::all();

        // Position the toolbar in the top-left corner, offset by menu bar height
        ImGui::SetNextWindowPos(ImVec2(10, 10 + menuBarHeight), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.85f);

        ImGuiWindowFlags toolbarFlags = ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoTitleBar;

        ImGui::Begin("##Toolbar", nullptr, toolbarFlags);

        ///TODO: Make this configurable
        const float buttonSize = 48.0f;
        const bool hasSurface = (bool)app.selectedSurface;
        // Disable selection tools when Alt is held (camera mode)
        const bool altPressed = app.keyboard.alt;

        for (const auto& action : actions) {
            if (!action.showInToolbar) {
                continue;
            }

            const bool isActive = action.isActive(app);
            // Use custom isEnabled if provided, otherwise fall back to requiresSurface check
            // Also disable all selection tools when Alt is pressed (camera mode)
            const bool canExecute =
                !altPressed &&
                (action.isEnabled ? action.isEnabled(app) : (!action.requiresSurface || hasSurface));
            const std::string tooltipStr =
                action.tooltip() + (!canExecute && action.requiresSurface && !hasSurface
                                        ? " (select a surface first)"
                                        : "");

            if (canExecute) {
                if (toolButton(
                        app.gui, action.iconPath(), tooltipStr.c_str(), isActive, false, buttonSize
                    )) {
                    app.doAction(action.execute);
                }
            } else {
                // Button is disabled, user cannot interact, button is greyed-out
                ImGui::BeginDisabled();
                toolButton(app.gui, action.iconPath(), tooltipStr.c_str(), false, true, buttonSize);
                ImGui::EndDisabled();
            }
            // Vertical layout - no SameLine()
        }

        ImGui::End();
    }

    void guiSubToolbar(App& app, float menuBarHeight)
    {
        if (!obj::eq_any(app.state(), App::State::idle, App::State::transform)) {
            return;
        }

        // Position the subtoolbar at the top center, offset by menu bar height
        ImGui::SetNextWindowPos(
            ImVec2(static_cast<float>(app.winSize.x()) * 0.5f, 10.0f + menuBarHeight), ImGuiCond_Always,
            ImVec2(0.5f, 0.0f)
        );
        ImGui::SetNextWindowBgAlpha(0.85f);

        ImGuiWindowFlags toolbarFlags = ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoTitleBar;

        ImGui::Begin("##SubToolbar", nullptr, toolbarFlags);

        const float buttonSize = 32.0f;
        const bool hasSurface = (bool)app.selectedSurface;
        // Disable toolbar when Alt is held (camera mode)
        const bool altPressed = app.keyboard.alt;

        if (!hasSurface || altPressed) {
            ImGui::BeginDisabled();
        }

        // Translate
        bool isTranslate = (app.curGizmo == app.gizTranslate);
        if (toolButton(
                app.gui, opts::resources / "icons" / "translate.png", "Translate", isTranslate,
                !hasSurface, buttonSize
            )) {
            if (app.selectedSurface && !app.selectedSurface->selected.empty()) {
                app.state(App::State::transform, app.gizTranslate);
            } else {
                app.state(App::State::idle, app.gizTranslate);
            }
        }
        ImGui::SameLine();

        // Rotate
        bool isRotate = (app.curGizmo == app.gizRotate);
        if (toolButton(
                app.gui, opts::resources / "icons" / "rotate-3d.png", "Rotate", isRotate,
                !hasSurface, buttonSize
            )) {
            if (app.selectedSurface && !app.selectedSurface->selected.empty()) {
                app.state(App::State::transform, app.gizRotate);
            } else {
                app.state(App::State::idle, app.gizRotate);
            }
        }
        ImGui::SameLine();

        // Scale
        bool isScale = (app.curGizmo == app.gizScale);
        if (toolButton(
                app.gui, opts::resources / "icons" / "scale-3d.png", "Scale", isScale, !hasSurface,
                buttonSize
            )) {
            if (app.selectedSurface && !app.selectedSurface->selected.empty()) {
                app.state(App::State::transform, app.gizScale);
            } else {
                app.state(App::State::idle, app.gizScale);
            }
        }
        ImGui::SameLine();

        // Local Coords
        bool isLocal = app.useLocalCoordinates;
        if (toolButton(
                app.gui, opts::resources / "icons" / "move-3d.png", "Local Coords", isLocal,
                !hasSurface, buttonSize
            )) {
            app.useLocalCoordinates = !app.useLocalCoordinates;
            if (app.selectedSurface && !app.selectedSurface->selected.empty()) {
                app.onSelection();
            }
        }

        if (!hasSurface || altPressed) {
            ImGui::EndDisabled();
        }

        ImGui::End();
    }

    // Forward declaration
    void guiSculpting(App& app, float menuBarHeight);

    void debugWindow(App& app)
    {
        ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        // Use 70% of the normal font size for the debug window
        ImGui::SetWindowFontScale(0.7f);
        if (ImGui::BeginTabBar("Tabs")) {
            if (ImGui::BeginTabItem("Debugging")) {
                static constexpr std::array<std::pair<App::ViewportDisplayMode, const char*>, 2u>
                    viewportDisplayModeChoices = {
                        std::pair{ App::ViewportDisplayMode::Shaded, "Shaded" },
                        std::pair{ App::ViewportDisplayMode::Flat, "Flat" },
                    };

                static size_t viewportDisplayModeChoice = 0;
                if (ImGui::BeginCombo(
                        "Viewport Display Mode",
                        viewportDisplayModeChoices[viewportDisplayModeChoice].second
                    )) {
                    for (size_t i = 0; i < viewportDisplayModeChoices.size(); i++) {
                        const auto& [method, name] = viewportDisplayModeChoices[i];
                        bool isSelected = (app.engineState.viewportDisplayMode == method);
                        if (ImGui::Selectable(name, &isSelected)) {
                            if (i != viewportDisplayModeChoice) {
                                app.engineState.viewportDisplayMode = method;
                                viewportDisplayModeChoice = i;
                            }
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                // create checkboxes for all members inside struct DebugDraw
                ImGui::Checkbox("Show Vertices", &app.engineState.showVertices);
                if (app.engineState.showVertices) {
                    ImGui::SliderFloat(
                        "Vertex Label Render Radius", &app.debugVertLabelDist, 0.1f, 100.0f
                    );
                }
                ImGui::Checkbox("Show Normal Vectors", &app.engineState.showNormals);
                ImGui::Checkbox("Show Wireframe", &app.engineState.isWireframe);
                ImGui::Checkbox("Show Halfedges", &app.engineState.showHalfEdges);
                ImGui::Checkbox("Per-Face Debugging", &app.debugHalfEdgeTwin);
                if (app.debugHalfEdgeTwin) {
                    if (app.hoveredSurface && app.hoveredSurface->hoveredFaceID != invalid_idx) {
                        const ispan_idx_t spanIdx =
                            app.hoveredSurface->gdata.spanIdxAt(app.hoveredSurface->hoveredFaceID);
                        const ispan_t hSpan =
                            app.hoveredSurface->gdata.getSpan(spanIdx);
                        Vector3u twins = Vector3u::Constant(invalid_idx);
                        Vector3u halfedges = Vector3u::Constant(invalid_idx);
                        Vector3u vertices = Vector3u::Constant(invalid_idx);
                        for (int i = 0; i < 3; i++) {
                            uint32_t h = 3 * app.hoveredSurface->hoveredFaceID + i;
                            if (hSpan.contains(h)) {
                                twins[i] = app.hoveredSurface->hTwin[h];
                                halfedges[i] = h;
                                vertices[i] = app.hoveredSurface->hVertex[h];
                            }
                        }

                        app.gui.text("face= f{}", halfedges[0] / 3);
                        app.gui.text("halfedges= {}", halfedges);
                        app.gui.text("twins= {}", twins);
                        app.gui.text("vertices= {}", vertices);
                    }
                }
                ImGui::Checkbox("Show Vertex Labels", &app.debugShowVertexLabels);
                ImGui::Checkbox("Show Metageometry", &app.showMetaGeom);
                ImGui::SliderFloat("interp voffset", &app.interpOffset, 0.0f, 1.0f);

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Modelling")) {
                guiChooseScene(app);
                if (!app.selectedSurface) {
                    ImGui::Text("select a surface");
                } else {
                    guiSlicePlane(app);
                    guiEdgeRemovalMode(app);
                    guiCutPatch(app);

                    // Button that remeshes the selected patches on the selected surface
                    if (ImGui::Button("(R) Remesh Patches")) {
                        app.remeshSelection();
                    }
                }

                // A dropdown list where the user can select a model to import
                if (ImGui::CollapsingHeader("Import Model")) {
                    const static std::pair<std::string, std::string> models[] = {
                        { "Plane", "subdivPlane.obj" },
                        { "Convex Ngon", "convexNgon.obj" },
                        { "Uneven Div Plane", "planeUnevenDiv.obj" },
                        { "Ring", "ring.obj" },
                        { "Suzanne", "suzanne.obj" },
                        { "Teapot", "teapot.obj" },
                        { "Underwear", "underwear.obj" },
                        { "Underwear Lowpoly", "underwearlp.obj" },
                        { "Icososphere", "icosphere.obj" },
                        { "Triangle", "triangle.obj" },
                        { "Unit Cube", "unitCube.obj" },
                    };
                    static size_t selectedModel = 0u;
                    if (ImGui::BeginCombo("Models", models[selectedModel].first.c_str())) {
                        for (size_t i = 0; i < std::size(models); ++i) {
                            const bool isSelected = (selectedModel == i);
                            if (ImGui::Selectable(models[i].first.c_str(), isSelected)) {
                                selectedModel = i;
                            }
                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::Button("Import")) {
                        Model::load(
                            opts::models / models[selectedModel].second, app.tree,
                            app.history
                        );
                    }
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("History")) {
                // Buttons to undo and redo operations
                if (ImGui::Button("Undo (Ctrl+Z)")) {
                    app.doAction([](App& app) { app.history.undo(); });
                }
                ImGui::SameLine();
                if (ImGui::Button("Redo (Ctrl+Shift+Z)")) {
                    app.doAction([](App& app) { app.history.redo(); });
                }

                // Show history
                app.gui.text(
                    "{} items, {}", app.history.size(),
                    formatBytes((double)app.history.totalStorage())
                );
                app.gui.text("Position: {}", app.history.position());

                // Show every item in history
                for (const auto& [i, op] :
                     ranges::enumerate(app.history.operations) | std::views::reverse) {
                    const std::string& label = fmt::format(
                        " - ({}) [{}] {}", i + 1, formatBytes((double)op->size()), op->label()
                    );
                    if (i >= app.history.position()) {
                        ImGui::TextDisabled(label.c_str());
                    } else {
                        ImGui::Text(label.c_str());
                    }
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Inspector")) {
                guiInspector(app);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Weights")) {
                guiWeightsTab(app);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
        ImGui::End();
    }
    // Static variable to track help window visibility
    static bool showHelpWindow = false;

    void updateGUI(App& app)
    {
        app.gui.beginFrame();

        // Main Menu Bar
        if (ImGui::BeginMainMenuBar()) {
            // Edit Menu
            if (ImGui::BeginMenu("Edit")) {
                const bool hasSurface = static_cast<bool>(app.selectedSurface);
                const bool hasSelection = hasSurface && !app.selectedSurface->selected.empty();

                if (ImGui::MenuItem("Duplicate Selection", "Ctrl+D", false, hasSelection)) {
                    EditorActions::execute("duplicate", app);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
                    app.doAction([](App& app) { app.history.undo(); });
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z")) {
                    app.doAction([](App& app) { app.history.redo(); });
                }
                ImGui::EndMenu();
            }

            // View Menu
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Reset Camera")) {
                    app.camControl = app.camControlDefault;
                    app.camControl.update(app.cam, app.winSize.cast<float>());
                }
                ImGui::EndMenu();
            }

            // History Menu
            if (ImGui::BeginMenu("History")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
                    app.doAction([](App& app) { app.history.undo(); });
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z")) {
                    app.doAction([](App& app) { app.history.redo(); });
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("History Items")) {
                    if (app.history.size() == 0) {
                        ImGui::TextDisabled("No history");
                    } else {
                        for (const auto& [i, op] :
                             ranges::enumerate(app.history.operations) | std::views::reverse) {
                            const std::string label = fmt::format(
                                "({}) {}", i + 1, op->label()
                            );
                            if (i >= app.history.position()) {
                                ImGui::TextDisabled("%s", label.c_str());
                            } else {
                                ImGui::Text("%s", label.c_str());
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            // Help Menu
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("Show Help")) {
                    showHelpWindow = true;
                }
                ImGui::EndMenu();
            }

            // Debug Menu
            if (ImGui::BeginMenu("Debug")) {
                if (ImGui::MenuItem("Toggle Debug Info", nullptr, app.showDebugGUI)) {
                    app.showDebugGUI = !app.showDebugGUI;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        // Get menu bar height to offset other GUI elements
        const float menuBarHeight = ImGui::GetFrameHeightWithSpacing();

        // Help Window
        if (showHelpWindow) {
            ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Help", &showHelpWindow)) {
                ImGui::TextWrapped("Patchwork Help");
                ImGui::Separator();

                ImGui::Text("Camera Controls:");
                ImGui::BulletText("Alt + Left Mouse: Orbit camera");
                ImGui::BulletText("Alt + Middle Mouse: Pan camera");
                ImGui::BulletText("Alt + Right Mouse: Zoom camera");
                ImGui::BulletText("Mouse Scroll: Zoom in/out");

                ImGui::Separator();
                ImGui::Text("Keyboard Shortcuts:");
                ImGui::BulletText("T: Translate tool");
                ImGui::BulletText("R: Rotate tool");
                ImGui::BulletText("S: Scale tool");
                ImGui::BulletText("E: Extrude");
                ImGui::BulletText("Ctrl+D: Duplicate");
                ImGui::BulletText("Ctrl+Z: Undo");
                ImGui::BulletText("Ctrl+Shift+Z: Redo");

                ImGui::Separator();
                ImGui::Text("Selection:");
                ImGui::BulletText("Left Click: Select patch");
                ImGui::BulletText("Ctrl + Left Click: Multi-select");
            }
            ImGui::End();
        }

        // Display the toolbars with tool buttons that represent editor actions
        guiToolbar(app, menuBarHeight);
        guiSubToolbar(app, menuBarHeight);

        // Display sculpting window when in sculpting mode
        if (app.state() == App::State::sculpting) {
            guiSculpting(app, menuBarHeight);
        }

        if (app.showDebugGUI) {
            debugWindow(app);
            guiInfo(app, windowFlagsStatic, menuBarHeight);
        }

        // Draw the error in a dismissable popup (1/3rd of previous size)
        ImVec2 popupSize(std::min(app.winSize.x(), 267), std::min(app.winSize.y(), 167));
        ImGui::SetNextWindowSize(popupSize);
        ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowPos(ImVec2(
            app.winSize.x() / 2.0 - popupSize.x / 2.0, app.winSize.y() / 2.0 - popupSize.y / 2.0
        ));
        if (!app.lastError.empty() && !ImGui::IsPopupOpen("Error Message")) {
            ImGui::OpenPopup("Error Message");
        }
        if (ImGui::BeginPopup("Error Message")) {
            if (!app.lastError.empty()) {
                ImGui::TextWrapped("%s", app.lastError.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("Dismiss")) {
                    app.lastError.clear();
                    ImGui::CloseCurrentPopup();
                }
            } else {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Draw instruction bar at bottom of screen for active tools
        const char* instruction = nullptr;
        if (app.gizSlicePlane && app.gizSlicePlane->isRender) {
            instruction = "Press Escape, Enter, or click Slice button to execute slice";
        } else if (app.state() == App::State::carving && app.carveDragging) {
            instruction = "Release mouse button to execute carve";
        }

        if (instruction) {
            ImGuiWindowFlags instructionFlags =
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize;
            ImGui::SetNextWindowPos(
                ImVec2(
                    static_cast<float>(app.winSize.x()) / 2.0f,
                    static_cast<float>(app.winSize.y()) - 40.0f
                ),
                ImGuiCond_Always, ImVec2(0.5f, 0.5f)  // Center horizontally
            );
            ImGui::SetNextWindowBgAlpha(0.75f);
            ImGui::Begin("##instruction_bar", nullptr, instructionFlags);
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "%s", instruction);
            ImGui::End();
        }

        // Display current state and modifier keys centered below sub toolbar
        {
            ImGuiWindowFlags stateFlags =
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
            // Position centered horizontally, below the sub toolbar (estimated 70px below menu bar)
            ImGui::SetNextWindowPos(
                ImVec2(static_cast<float>(app.winSize.x()) * 0.5f, menuBarHeight + 70.0f),
                ImGuiCond_Always, ImVec2(0.5f, 0.0f)  // Anchor at top-center
            );
            ImGui::SetNextWindowBgAlpha(0.5f);
            ImGui::Begin("##state_display", nullptr, stateFlags);

            // Colors for modifier indicators
            const ImVec4 activeColor(0.4f, 1.0f, 0.4f, 1.0f);    // Green when pressed
            const ImVec4 faintColor(0.45f, 0.45f, 0.45f, 0.8f);  // Faint gray when not pressed

            // Show mouse action hints based on state
            if (obj::eq_any(app.state(), App::State::idle, App::State::transform)) {
                // Alt camera control indicator
                if (app.keyboard.alt) {
                    ImGui::TextColored(activeColor, "Alt + Left Mouse: rotate camera");
                } else {
                    ImGui::TextColored(faintColor, "Alt + Left Mouse: rotate camera");
                }
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 0.6f), " | ");
                ImGui::SameLine();

                // Selection indicator
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Left Mouse: select");

                // Show modifier keys available during transform (Shift/Ctrl for uniform scale)
                if (app.state() == App::State::transform) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 0.6f), " | ");
                    ImGui::SameLine();

                    // Shift indicator - lights up when pressed
                    if (app.keyboard.shift) {
                        ImGui::TextColored(activeColor, "Shift: uniform");
                    } else {
                        ImGui::TextColored(faintColor, "Shift");
                    }

                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 0.6f), " ");
                    ImGui::SameLine();

                    // Ctrl indicator - lights up when pressed
                    if (app.keyboard.ctrl) {
                        ImGui::TextColored(activeColor, "Ctrl: uniform");
                    } else {
                        ImGui::TextColored(faintColor, "Ctrl");
                    }
                }
            } else {
                // For other states, show the state name
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "State: %s", stateToString(app.state()));
            }
            ImGui::End();
        }

        app.gui.endFrame();
    }

    // Display real-time performance information
    void guiInfo(App& app, const ImGuiWindowFlags flagsNoWindow, float menuBarHeight)
    {
        const auto rowText = [](std::string_view label, std::string_view value) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const float colWidth = ImGui::GetColumnWidth();
            const ImVec2 textSize = ImGui::CalcTextSize(label.data(), label.data() + label.size());
            ImVec2 pos = ImGui::GetCursorPos();
            pos.x += colWidth - textSize.x - ImGui::GetStyle().CellPadding.x * 2.0f;
            ImGui::SetCursorPos(pos);
            ImGui::TextUnformatted(label.data(), label.data() + label.size());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(value.data(), value.data() + value.size());
        };

        // Position at top-right corner, using pivot (1, 0) to anchor from top-right, offset by menu bar
        ImGui::SetNextWindowPos(
            ImVec2(static_cast<float>(app.winSize.x()) - 10.0f, 10.0f + menuBarHeight), ImGuiCond_Always,
            ImVec2(1.0f, 0.0f)  // Pivot: right edge, top edge
        );
        ImGui::SetNextWindowSize(ImVec2(0, 0));
        ImGui::SetNextWindowBgAlpha(0.35f);
        if (ImGui::Begin("info box", nullptr, flagsNoWindow)) {
            // Use 70% of the normal font size for the info box
            ImGui::SetWindowFontScale(0.7f);
            ImGui::PushFont(app.gui.monospaceFont);
            constexpr ImGuiTableFlags tableFlags =
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg;

            if (ImGui::BeginTable("info_table", 2, tableFlags)) {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                if (Surface::g_MacroPolyMode) {
                    rowText("Macropoly Mode", "ON");
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Build");
                ImGui::TableSetColumnIndex(1);
                if constexpr (g_debugMode) {
                    ImGui::TextColored(convert<ImVec4>(rgba(0.4f, 1.0f, 0.4f)), "Debug Mode");
                } else {
                    ImGui::TextColored(convert<ImVec4>(rgba(1.0f, 0.8f, 0.4f)), "Release Mode");
                }

                rowText("FPS", fmt::format("{:.1f}", ImGui::GetIO().Framerate));
                rowText(
                    "Camera Position",
                    fmt::format(
                        "{:.3f}, {:.3f}, {:.3f}", app.cam.pos.x(), app.cam.pos.y(), app.cam.pos.z()
                    )
                );
                rowText(
                    "Camera Rotation",
                    fmt::format(
                        "{:.3f}, {:.3f}, {:.3f}", app.cam.rot.x(), app.cam.rot.y(), app.cam.rot.z()
                    )
                );

                ImGui::EndTable();
            }

            // Function timers table (below primary info)
            if (!StopWatch::funcTimers.empty() && ImGui::BeginTable("func_timers", 3, tableFlags)) {
                ImGui::TableSetupColumn("function", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                ImGui::TableSetupColumn("avg", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("max", ImGuiTableColumnFlags_WidthStretch);

                for (const auto& [label, timer] : StopWatch::funcTimers) {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    const float colWidth = ImGui::GetColumnWidth();
                    const ImVec2 textSize =
                        ImGui::CalcTextSize(label.data(), label.data() + label.size());
                    ImVec2 pos = ImGui::GetCursorPos();
                    pos.x += colWidth - textSize.x - ImGui::GetStyle().CellPadding.x * 2.0f;
                    ImGui::SetCursorPos(pos);
                    ImGui::TextUnformatted(label.data(), label.data() + label.size());

                    ImGui::TableSetColumnIndex(1);
                    const std::string avgStr = StopWatch::timeString(timer->milliseconds());
                    ImGui::TextUnformatted(avgStr.data(), avgStr.data() + avgStr.size());

                    ImGui::TableSetColumnIndex(2);
                    const double maxVal = timer->maximum();
                    const std::string maxStr = maxVal > 0.0 ? StopWatch::timeString(maxVal) : "-";
                    ImGui::TextUnformatted(maxStr.data(), maxStr.data() + maxStr.size());
                }

                ImGui::EndTable();
            }
            ImGui::PopFont();
        }
        ImGui::End();
    }

    void guiEdgeRemovalMode(App& app)
    {
        if (app.selectedSurface) {
            bool allBorders = true;
            uint32_t borderCount = 0;
            uint64_t borderID = invalid_idx;
            for (const uint64_t metaID : app.selectedSurface->selected) {
                if (!Surface::border_idx_t::valid(metaID)) {
                    allBorders = false;
                    break;
                } else {
                    borderCount++;
                    if (borderCount == 1) {
                        borderID = metaID;
                    }
                }
            }

            if (allBorders == false || borderCount != 1) {
                ImGui::BeginDisabled();
                ImGui::Button("Remove Edge");
                ImGui::EndDisabled();
            } else {
                if (ImGui::Button("Remove Edge")) {
                    app.state(App::State::idle);
                    app.doAction([&](App& app) {
                        if (!app.selectedSurface) {
                            return;
                        }
                        app.selectedSurface->joinPatches(borderID);
                    });
                }
            }
        }
    }

    void guiCutPatch(App& app)
    {
        if (!app.gizSlicePlane->isRender && ImGui::Button("Cut Patch")) {
            app.state(App::State::slicing);
        } else if (app.gizSlicePlane->isRender &&
                   (ImGui::Button("Cut") || app.keyboard.pressed.contains(GLFW_KEY_ENTER))) {
            bool allPatches = true;
            uint32_t patchCount = 0;
            uint64_t patchID = invalid_idx;
            for (const uint64_t metaID : app.selectedSurface->selected) {
                if (!Surface::patch_idx_t::valid(metaID)) {
                    allPatches = false;
                    break;
                } else {
                    patchCount++;
                    if (patchCount == 1) {
                        patchID = metaID;
                    }
                }
            }
            if (allPatches == true && patchCount == 1) {
                app.doAction([&](App& app) {
                    if (!app.selectedSurface) {
                        return;
                    }
                    app.selectedSurface->slicePlane(
                        app.gizSlicePlane->transform.rotation().col(2).normalized(),
                        app.gizSlicePlane->transform.translation(), patchID
                    );
                });
            }
            app.state(App::State::idle);
        }
    }

    void guiSlicePlane(App& app)
    {
        if (Surface::g_MacroPolyMode) {
            return;
        }

        if (!app.gizSlicePlane->isRender && ImGui::Button("Slice Plane")) {
            app.state(App::State::slicing);
        } else if (app.gizSlicePlane->isRender &&
                   (ImGui::Button("Slice") || app.keyboard.pressed.contains(GLFW_KEY_ENTER))) {
            app.doAction([&](App& app) {
                if (!app.selectedSurface) {
                    return;
                }
                app.selectedSurface->slicePlane(
                    app.gizSlicePlane->transform.rotation().col(2).normalized(),
                    app.gizSlicePlane->transform.translation(), invalid_idx
                );
            });
            app.state(App::State::idle);
        }
    }

    void guiWeightsTab(App& app)
    {
        ImGui::Text("Weight Solving Parameters");
        ImGui::Separator();

        // Solver selection
        ImGui::Checkbox("Use Iterative Solver", &Surface::Weights::useIterative);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Use iterative solver (true) or conjugate gradients (false)");
        }

        if (Surface::Weights::useIterative) {
            ImGui::Separator();
            ImGui::Text("Iterative Solver Settings");

            // Max iterations
            int maxIters = static_cast<int>(Surface::Weights::maxIters);
            if (ImGui::SliderInt("Max Iterations", &maxIters, 1, 2500)) {
                Surface::Weights::maxIters = static_cast<uint32_t>(maxIters);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Maximum number of iterations for convergence");
            }

            // Tolerance
            ImGui::SliderFloat(
                "Tolerance", &Surface::Weights::tol, 1e-14f, 1e-2f, "%.3e",
                ImGuiSliderFlags_Logarithmic
            );
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Convergence tolerance");
            }

            // Omega (relaxation parameter)
            ImGui::SliderFloat("Omega (Relaxation)", &Surface::Weights::omega, 0.1f, 2.0f, "%.3f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Relaxation parameter (0 < omega <= 1, SOR if Gauss-Seidel)");
            }

            // Gauss-Seidel vs Jacobi
            ImGui::Checkbox("Gauss-Seidel", &Surface::Weights::gaussSeidel);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Use Gauss-Seidel (true) or Jacobi (false) iteration");
            }

            ImGui::Separator();

            // Multi-frame processing
            ImGui::Checkbox("Multi-Frame Processing", &Surface::Weights::multiFrame);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Spread weight solving across multiple frames for better responsiveness"
                );
            }

            if (Surface::Weights::multiFrame) {
                // Iterations per frame
                uint32_t itersMin = 1u, itersMax = 500u;
                ImGui::SliderScalar(
                    "Iters per Frame", ImGuiDataType_U32, &Surface::Weights::iterationsPerFrame,
                    &itersMin, &itersMax, "%u"
                );
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Number of solver iterations to execute per frame (higher = faster "
                        "convergence, more frame time)"
                    );
                }
            }

            ImGui::Separator();
            ImGui::Text("Image Button Demo:");

            // Example image buttons using matcap textures
            if (app.gui.imageButton((opts::resources / "textures" / "matcaps" / "mc1.png").string(), 32, "Matcap 1")) {
                // Handle button click
            }
            ImGui::SameLine();
            if (app.gui.imageButton((opts::resources / "textures" / "matcaps" / "mc2.png").string(), 32, "Matcap 2")) {
                // Handle button click
            }
            ImGui::SameLine();
            if (app.gui.imageButton((opts::resources / "textures" / "matcaps" / "mc3.png").string(), 32, "Matcap 3")) {
                // Handle button click
            }
        }
    }

    void guiSculpting(App& app, float menuBarHeight)
    {
        ImGui::SetNextWindowPos(ImVec2(100, 100 + menuBarHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(250, 300), ImGuiCond_Always);

        if (ImGui::Begin(
                "Sculpting", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoMove
            )) {
            if (app.sculptingConfig.brushType != Sculpting::Brush::paint) {
                ImGui::Checkbox("Matcap", &app.sculptingConfig.isMatcap);
            }

            static constexpr std::array<std::pair<Sculpting::Brush, const char*>, 5u>
                sculptBrushChoices = { std::pair{ Sculpting::Brush::clay, "Clay" },
                                       std::pair{ Sculpting::Brush::mask, "Mask" },
                                       std::pair{ Sculpting::Brush::paint, "Paint" },
                                       std::pair{ Sculpting::Brush::smooth, "Smooth" },
                                       std::pair{ Sculpting::Brush::move, "Move" } };

            static size_t sculptBrushChoice = 0;
            if (ImGui::BeginCombo("Brush", sculptBrushChoices[sculptBrushChoice].second)) {
                for (size_t i = 0; i < sculptBrushChoices.size(); i++) {
                    const auto& [method, name] = sculptBrushChoices[i];
                    bool isSelected = (app.sculptingConfig.brushType == method);
                    if (ImGui::Selectable(name, &isSelected)) {
                        if (i != sculptBrushChoice) {
                            app.sculptingConfig.brushType = method;
                            sculptBrushChoice = i;
                        }
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::DragFloat("Size", &app.sculptingConfig.brushRadius, 0.01f, 0.01f, 1.0f);
            ImGui::DragFloat(
                "Intensity", &app.sculpt.brushIntensityMap[app.sculptingConfig.brushType], 0.01f,
                0.01f, 10.0f
            );
            ImGui::DragFloat("Falloff", &app.sculptingConfig.brushFalloff, 0.01f, 0.01f, 1.0f);
            ImGui::Checkbox("Symmetry", &app.sculptingConfig.isSymmetry);

            if (app.sculptingConfig.brushType == Sculpting::Brush::mask) {
                ImGui::Separator();
                if (ImGui::Button("Invert Mask")) {
                    if (app.sculpt.target) {
                        app.sculpt.globalGpuOperation(
                            app.sculptingConfig, Sculpting::MaskOp::invert
                        );
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Mask")) {
                    if (app.sculpt.target) {
                        app.sculpt.globalGpuOperation(
                            app.sculptingConfig, Sculpting::MaskOp::clear
                        );
                    }
                }
            } else if (app.sculptingConfig.brushType == Sculpting::Brush::paint) {
                ImGui::Separator();
                ImGui::ColorEdit4("Color", app.sculptingConfig.brushColor.data());
                if (ImGui::Button("Fill")) {
                    if (app.sculpt.target) {
                        app.sculpt.globalGpuOperation(app.sculptingConfig);
                    }
                }
            }
        }
        ImGui::End();
    }

    void guiInspector(App& app)
    {
        NodeWrapper<Surface>& s = app.selectedSurface;
        // List all selected metageometry from the selected surface in a dropdown. When picked,
        // the clicked metageometry will have its connected metageometry shown in the window as
        // a list. List items clicked will change the selection only if they are not already
        // selected.
        if (s && !s->selected.empty()) {
            static uint64_t inspectedMetaGeomID = 0;
            static uint64_t focusMetaGeomID = 0;
            if (!s->selected.contains(inspectedMetaGeomID)) {
                inspectedMetaGeomID = *s->selected.begin();
                focusMetaGeomID = 0;
            }

            // Show stats about the surface
            ImGui::SeparatorText("Surface Stats");
            ImGui::Text("vertex capacity: %lu", s->vdata.capacity());
            ImGui::Text("face capacity: %lu", s->gdata.capacity());
            ImGui::Text("buffer mem usage: %s", formatBytes(s->totalMemory()).c_str());

            ImGui::Separator();

            app.gui.text("hovered: {}", index_type::idx(app.hovered.metaID));
            if (ImGui::BeginCombo("Geom", fmt::format("{}", inspectedMetaGeomID).c_str())) {
                for (const uint64_t id : s->selected) {
                    if (ImGui::Selectable(
                            fmt::format(
                                "[{}] {}", Surface::indexTypeLabel(index_type::tag(id)),
                                index_type::idx(id)
                            )
                                .c_str(),
                            s->selected.contains(id)
                        )) {
                        inspectedMetaGeomID = id;
                        app.curGizmo->transform.translation() = s->centroid({ id });
                    }
                }
                ImGui::EndCombo();
            }

            // Some info about currently inspected metageometry
            ImGui::SeparatorText("Info");
            switch (index_type::tag(inspectedMetaGeomID)) {
                case Surface::border_idx_t::tagval: {
                    const Surface::border_idx_t borderID(inspectedMetaGeomID);
                    const std::shared_ptr<Border> b = s->borders.at(borderID.bytes);
                    app.gui.text("Border {:#x}", index_type::idx(inspectedMetaGeomID));
                    ImGui::Text("Length: %lu", b->length());
                    ImGui::Text("Is Loop: %s", b->isLoop() ? "true" : "false");
                    ImGui::Text(
                        "Vertex IDs: [%d, %d]", b->indices()[0], b->indices()[b->maxIndex()]
                    );
                    break;
                }
                case Surface::patch_idx_t::tagval: {
                    const Surface::patch_idx_t patchID(inspectedMetaGeomID);
                    app.gui.text("Patch {:#x}", index_type::idx(inspectedMetaGeomID));
                    ImGui::Text("Vertices: %lu", s->vdata.getSpan(patchID.idx()).length());
                    ImGui::Text("Faces: %lu", s->gdata.getSpan(patchID.idx()).length());
                    ImGui::Text("First vertex ID: %lu", s->vdata.getSpanStart(patchID.__idx));
                    break;
                }
                case Surface::point_idx_t::tagval: {
                    const auto& point = s->points.at(inspectedMetaGeomID);
                    app.gui.text("Point {:#x}", index_type::idx(inspectedMetaGeomID));
                    app.gui.text("Position: {}", point->position());
                    app.gui.text("Vidx: {}", point->vIdx());
                    break;
                }
            }

            ImGui::SeparatorText("Connected Metageometry");
            std::vector<uint64_t> connectedList;
            if (index_type::tag(inspectedMetaGeomID) != Surface::border_idx_t::tagval) {
                for (auto borderID : s->getBorders(inspectedMetaGeomID)) {
                    connectedList.push_back(borderID.bytes);
                }
            }
            if (index_type::tag(inspectedMetaGeomID) != Surface::patch_idx_t::tagval) {
                for (auto patchID : s->getPatches(inspectedMetaGeomID)) {
                    connectedList.push_back(patchID.bytes);
                }
            }
            if (index_type::tag(inspectedMetaGeomID) != Surface::point_idx_t::tagval) {
                for (auto pointID : s->getPoints(inspectedMetaGeomID)) {
                    connectedList.push_back(pointID.bytes);
                }
            }

            // List of connected metageometry, clicking on one will move the gizmo to it
            for (const uint64_t mID : connectedList) {
                // Color the button based on selection state
                ImVec4 color = ImVec4(0.05f, 0.07f, 0.09f, 1.0f);
                if (s->selected.contains(mID)) {
                    color = ImVec4(0.3f, 0.4f, 0.54f, 1.0f);
                }
                if (focusMetaGeomID == mID) {
                    color = ImVec4(0.67f, 0.69f, 0.45f, 1.0f);
                }
                ImGui::PushStyleColor(ImGuiCol_Button, color);
                ImGui::PushItemWidth(ImGui::GetColumnWidth());
                const bool clicked = ImGui::Button(
                    fmt::format(
                        "[{}] {}", Surface::indexTypeLabel(index_type::tag(mID)),
                        index_type::idx(mID)
                    )
                        .c_str()
                );
                ImGui::PopItemWidth();
                ImGui::PopStyleColor();
                if (clicked) {
                    if (focusMetaGeomID != mID) {
                        // Clear highlight for previously focused metageometry
                        if (focusMetaGeomID != 0u) {
                            switch (index_type::tag(focusMetaGeomID)) {
                                case Surface::border_idx_t::tagval:
                                    s->borders.at(focusMetaGeomID)->highlighted = false;
                                    break;
                                case Surface::patch_idx_t::tagval:
                                    s->patches.at(focusMetaGeomID)->highlighted = false;
                                    break;
                                case Surface::point_idx_t::tagval:
                                    s->points.at(focusMetaGeomID)->highlighted = false;
                                    break;
                            }
                        }

                        // Highlight the focused metageometry
                        switch (index_type::tag(mID)) {
                            case Surface::border_idx_t::tagval:
                                s->borders.at(mID)->highlighted = true;
                                break;
                            case Surface::patch_idx_t::tagval:
                                s->patches.at(mID)->highlighted = true;
                                break;
                            case Surface::point_idx_t::tagval:
                                s->points.at(mID)->highlighted = true;
                                break;
                        }
                        app.curGizmo->transform.translation() = s->centroid({ mID });

                        $debug(
                            "focused on [{}]{}", Surface::indexTypeLabel(index_type::tag(mID)),
                            index_type::idx(mID)
                        );
                    } else if (focusMetaGeomID == mID) {
                        s->clearSelection();
                        s->addToSelection(mID);
                        app.onSelection();
                    }
                    focusMetaGeomID = mID;
                }
            }
        } else {
            ImGui::Text("No surface selected");
        }
    }

    void guiChooseScene(App& app)
    {
        if (ImGui::BeginCombo("Scenes", "load a scene...")) {
            auto it = scene::scenes.begin();
            for (size_t i = 0; i < scene::scenes.size(); i++) {
                if (ImGui::Selectable(it->first.c_str())) {
                    app.loadScene(it->first);
                    break;
                }
                it++;
            }
            ImGui::EndCombo();
        }
    }

}  // namespace gui
