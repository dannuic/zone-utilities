#include "scene.h"
#include "imgui_glfw.h"
#include <gtc/matrix_transform.hpp>

#include "static_geometry.h"

Scene::Scene() {
}

Scene::~Scene() {
	UnregisterAllModules();
}

enum MainHotkeys : int
{
	MainHotkeyQuit,
	MainHotkeyOpen,
	MainHotkeySave,
	MainHotkeyToggleOptions,
	MainHotkeyToggleDebug,
};

void Scene::Init(GLFWwindow *win, int width, int height) {
	m_window = win;
	m_width = width;
	m_height = height;
	m_name[0] = 0;

	m_hor_angle = 3.14f;
	m_ver_angle = 0.0f;
	m_fov = 45.0f;
	m_near_clip = 0.1f;
	m_far_clip = 15000.0f;
	m_last_time = 0.0f;
	m_first_input = true;

	for(int i = 0; i < 512; ++i) {
		m_key_status[i] = GLFW_RELEASE;
	}

	for (int i = 0; i < 5; ++i) {
		m_mouse_status[i] = GLFW_RELEASE;
	}
	
	m_show_open = false;
	m_show_options = false;
	m_show_debug = false;
	m_render_collide = true;
	m_render_non_collide = true;
	m_render_volume = true;

	glCullFace(GL_BACK);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glClearColor(0.5f, 0.5f, 0.5f, 0.5f);
	glViewport(0, 0, width, height);
	glBlendFunc(GL_ZERO, GL_SRC_COLOR);

	glfwSetInputMode(win, GLFW_STICKY_MOUSE_BUTTONS, 1);
	glfwSetInputMode(win, GLFW_STICKY_KEYS, 1);
	ImGui_ImplGlfwGL3_Init(win, true);

	RegisterHotkey(this, MainHotkeyQuit, GLFW_KEY_Q, false, true, false);
	RegisterHotkey(this, MainHotkeyOpen, GLFW_KEY_O, true, false, false);
	RegisterHotkey(this, MainHotkeySave, GLFW_KEY_S, true, false, false);
	RegisterHotkey(this, MainHotkeyToggleOptions, GLFW_KEY_O, false, true, false);
	RegisterHotkey(this, MainHotkeyToggleDebug, GLFW_KEY_G, false, true, false);

	m_physics.reset(new EQPhysics());
#ifdef EQEMU_GL_DEP
	m_shader.reset(new ShaderProgram("shaders/basic.vert", "shaders/basic.frag"));
#else
	m_shader.reset(new ShaderProgram("shaders/basic130.vert", "shaders/basic130.frag"));
#endif

	m_model = m_shader->GetUniformLocation("Model");
	m_view = m_shader->GetUniformLocation("View");
	m_proj = m_shader->GetUniformLocation("Proj");
	m_tint = m_shader->GetUniformLocation("Tint");
}

void Scene::LoadScene(const char *zone_name) {
	m_name = zone_name;
	m_hor_angle = 3.14f;
	m_ver_angle = 0.0f;
	m_camera_loc.x = 0.0f;
	m_camera_loc.y = 0.0f;
	m_camera_loc.z = 0.0f;

	m_zone_geometry.reset(ZoneMap::LoadMapFile(zone_name));
	if(m_zone_geometry) {
		m_physics.reset(new EQPhysics());

		WaterMap *w_map = WaterMap::LoadWaterMapfile(zone_name);
		m_physics->SetCollidableWorld(m_zone_geometry->GetCollidableVerts(), m_zone_geometry->GetCollidableInds());
		m_physics->SetNonCollidableWorld(m_zone_geometry->GetNonCollidableVerts(), m_zone_geometry->GetNonCollidableInds());
		m_physics->SetWaterMap(w_map);

		//create models from the loaded stuff here...
		StaticGeometry *m = new StaticGeometry();
		m->GetVerts() = m_zone_geometry->GetCollidableVerts();
		m->GetInds() = m_zone_geometry->GetCollidableInds();
		m->SetTint(glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
		m->Compile();
		m_collide_mesh_entity.reset(m);

		m = new StaticGeometry();
		m->GetVerts() = m_zone_geometry->GetNonCollidableVerts();
		m->GetInds() = m_zone_geometry->GetNonCollidableInds();
		m->SetTint(glm::vec4(0.5f, 0.7f, 1.0f, 1.0f));
		m->Compile();
		m_non_collide_mesh_entity.reset(m);

		m = new StaticGeometry();
		if(w_map) {
			w_map->CreateMeshFrom(m->GetVerts(), m->GetInds());
		}
		m->SetTint(glm::vec4(0.0f, 0.0f, 0.8f, 0.2f));
		m->Compile();
		m_volume_mesh_entity.reset(m);
	}

	for(auto &module : m_modules) {
		module->OnSceneLoad(m_name.c_str());
	}
}

void Scene::Render() {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	ImGui_ImplGlfwGL3_NewFrame();

	m_shader->Use();

	glm::mat4 model = glm::mat4(1.0);

	m_model.SetValueMatrix4(1, false, &model[0][0]);
	m_view.SetValueMatrix4(1, false, &m_camera_view[0][0]);
	m_proj.SetValueMatrix4(1, false, &m_camera_proj[0][0]);

	//render all our models here
	if(m_render_collide && m_collide_mesh_entity) {
		glm::vec4 tint = m_collide_mesh_entity->GetTint();
		m_tint.SetValuePtr4(1, &tint[0]);
		m_collide_mesh_entity->Draw();

		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		tint.x = 0.0f;
		tint.y = 0.0f;
		tint.z = 0.0f;
		tint.w = 0.0f;
		m_tint.SetValuePtr4(1, &tint[0]);
		m_collide_mesh_entity->Draw();

		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}

	if(m_render_non_collide && m_non_collide_mesh_entity) {
		glm::vec4 tint = m_non_collide_mesh_entity->GetTint();
		m_tint.SetValuePtr4(1, &tint[0]);
		m_non_collide_mesh_entity->Draw();

		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		tint.x = 0.0f;
		tint.y = 0.0f;
		tint.z = 0.0f;
		tint.w = 0.0f;
		m_tint.SetValuePtr4(1, &tint[0]);
		m_non_collide_mesh_entity->Draw();

		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}

	if(m_render_volume && m_volume_mesh_entity) {
		glEnable(GL_BLEND);
		glm::vec4 tint = m_volume_mesh_entity->GetTint();
		m_tint.SetValuePtr4(1, &tint[0]);
		m_volume_mesh_entity->Draw();
		glDisable(GL_BLEND);
	}

	for (auto &l : m_registered_entities) {
		for (auto &e : l.second) {
			glm::vec4 tint = e->GetTint();
			m_tint.SetValuePtr4(1, &tint[0]);
			
			auto &pos = e->GetLocation();
			model[3][0] = pos.x;
			model[3][1] = pos.y;
			model[3][2] = pos.z;
			m_model.SetValueMatrix4(1, false, &model[0][0]);
			e->Draw();
		}
	}

	//render our main menu
	ImGui::BeginMainMenuBar();
	RenderMainMenu();
	RenderModulesMenu();
	ImGui::EndMainMenuBar();

	RenderUI();

	//render our modules ui stuff
	for(auto &module : m_modules) {
		if(module->GetRunning() && module->GetUnpaused()) {
			module->OnDrawUI();
		}
	}

	ImGui::Render();
	glfwSwapBuffers(m_window);
}

void Scene::RenderMainMenu() {
	if(ImGui::BeginMenu("File"))
	{
		ImGui::MenuItem("Open", "Ctrl+O", &m_show_open);
		bool can_save = false;
		for(auto &module : m_modules) {
			if(module->GetRunning() && module->GetUnpaused() && module->CanSave()) {
				can_save = true;
				break;
			}
		}

		if(ImGui::MenuItem("Save", "Ctrl+S", nullptr, can_save)) {
			for(auto &module : m_modules) {
				if(module->GetRunning() && module->GetUnpaused() && module->CanSave()) {
					module->Save();
				}
			}
		}
		ImGui::Separator();
		ImGui::MenuItem("Show Options", "Alt+O", &m_show_options);
		ImGui::MenuItem("Show Debug", "Alt+G", &m_show_debug);
		ImGui::Separator();
		if(ImGui::MenuItem("Quit", "Alt+Q")) { glfwSetWindowShouldClose(m_window, 1); }
		ImGui::EndMenu();
	}
}

void Scene::RenderUI() {
	if(m_show_debug) {
		ImGui::Begin("Debug");
		ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::Text("Loc: (%.2f, %.2f, %.2f)", m_camera_loc.x, m_camera_loc.y, m_camera_loc.z);
		ImGui::Text("Best floor: %.2f", m_physics->FindBestFloor(m_camera_loc, nullptr, nullptr));
		ImGui::Text("InLiquid: %s", m_physics->InLiquid(m_camera_loc) ? "true" : "false");
		if(GetZoneGeometry()) {
			auto zone_geo = GetZoneGeometry();
			ImGui::Text("Min: (%.2f, %.2f, %.2f)", zone_geo->GetCollidableMin().x, zone_geo->GetCollidableMin().y, zone_geo->GetCollidableMin().z);
			ImGui::Text("Max: (%.2f, %.2f, %.2f)", zone_geo->GetCollidableMax().x, zone_geo->GetCollidableMax().y, zone_geo->GetCollidableMax().z);
		}
		ImGui::End();
	}
	
	if(m_show_options) {
		ImGui::Begin("Options");
		ImGui::Checkbox("Render Collidable Mesh", &m_render_collide);
		ImGui::Checkbox("Render Non-Collidable Mesh", &m_render_non_collide);
		ImGui::Checkbox("Render Volumes", &m_render_volume);
		ImGui::End();
	}
	
	for(auto &module : m_modules) {
		if(module->HasWork()) {
			m_show_open = false;
		}
	}

	if(m_show_open) {
		ImGui::OpenPopup("Open Zone");
		m_show_open = false;
	}

	if(ImGui::BeginPopupModal("Open Zone", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
bool use_popup = true;
for (auto &module : m_modules) {
	if (module->HasWork()) {
		ImGui::CloseCurrentPopup();
		use_popup = false;
		break;
	}
}
if (use_popup) {
	static char zone_name[256];
	if (ImGui::InputText("Zone Name", zone_name, 256, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank))
	{
		ImGui::CloseCurrentPopup();
		LoadScene(zone_name);
		strcpy(zone_name, "");
	}

	if ((glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)) {
		strcpy(zone_name, "");

		ImGui::CloseCurrentPopup();
	}

	if (ImGui::IsItemHovered() || (ImGui::IsRootWindowOrAnyChildFocused() && !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0)))
		ImGui::SetKeyboardFocusHere(-1);

	if (ImGui::Button("OK", ImVec2(120, 0))) {
		ImGui::CloseCurrentPopup();
		LoadScene(zone_name);
		strcpy(zone_name, "");
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0))) {
		ImGui::CloseCurrentPopup();
		strcpy(zone_name, "");
	}
}
ImGui::EndPopup();
	}
}

void Scene::RenderModulesMenu() {
	if (ImGui::BeginMenu("Modules"))
	{
		for (auto &module : m_modules) {
			if (!module->GetRunning()) {
				if (ImGui::MenuItem(module->GetName(), nullptr, &module->GetRunning())) {
					module->OnResume();
				}
			}
			else {
				if (ImGui::MenuItem(module->GetName())) {
					if (module->GetUnpaused()) {
						module->OnSuspend();
					}
					else {
						module->OnResume();
					}

					module->SetUnpaused(!module->GetUnpaused());
				}
			}
		}
		ImGui::EndMenu();
	}

	//go through and render all active modules...
	for (auto &module : m_modules) {
		if (module->GetRunning() && module->GetUnpaused()) {
			module->OnDrawMenu();
		}
	}
}

void Scene::Tick() {
	glfwPollEvents();

	if (!TryHotkey()) {
		ProcessSceneInput();
	}
}

void Scene::ProcessSceneInput() {
	auto &io = ImGui::GetIO();

	if (m_first_input) {
		m_last_time = glfwGetTime();
		m_first_input = false;
	}

	double current_time = glfwGetTime();
	float delta_time = float(current_time - m_last_time);

	if (!io.WantCaptureMouse && glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
		double x_pos, y_pos;
		glfwGetCursorPos(m_window, &x_pos, &y_pos);
		glfwSetCursorPos(m_window, m_width / 2, m_height / 2);

		m_hor_angle += 0.005f * float(m_width / 2 - x_pos);
		m_ver_angle += 0.005f * float(m_height / 2 - y_pos);
	}

	glm::vec3 direction(cos(m_ver_angle) * sin(m_hor_angle), sin(m_ver_angle), cos(m_ver_angle) * cos(m_hor_angle));
	glm::vec3 right = glm::vec3(sin(m_hor_angle - 3.14f / 2.0f), 0, cos(m_hor_angle - 3.14f / 2.0f));
	glm::vec3 up = glm::cross(right, direction);

	float speed = 50.0f;

	if(!io.WantCaptureKeyboard && (
		glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || 
		glfwGetKey(m_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)) {
		speed *= 6.0f;
	}

	if(!io.WantCaptureKeyboard && glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) {
		m_camera_loc += direction * delta_time * speed;
	}

	if(!io.WantCaptureKeyboard && glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS) {
		m_camera_loc -= direction * delta_time * speed;
	}

	if(!io.WantCaptureKeyboard && glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS) {
		m_camera_loc += right * delta_time * speed;
	}

	if(!io.WantCaptureKeyboard && glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS) {
		m_camera_loc -= right * delta_time * speed;
	}

	m_camera_proj = glm::perspective(m_fov, (float)m_width / (float)m_height, m_near_clip, m_far_clip);
	m_camera_view = glm::lookAt(m_camera_loc, m_camera_loc + direction, up);

	bool click_to_process = false;
	bool process_click[5] = { false };
	if (!io.WantCaptureMouse) {
		for (int i = 0; i < 5; ++i) {
			if (io.MouseDown[i] == GLFW_RELEASE && m_mouse_status[i] == GLFW_PRESS) {
				process_click[i] = true;
				click_to_process = true;
			}
		}

		for (int i = 0; i < 5; ++i) {
			m_mouse_status[i] = io.MouseDown[i];
		}
	}

	if (click_to_process) {
		//get collide world click loc
		//get non-collide world click loc

		double x_pos, y_pos;
		glfwGetCursorPos(m_window, &x_pos, &y_pos);
		glm::vec3 start;
		glm::vec3 end;
		getClickVectors(x_pos, (double)m_height - y_pos, start, end);

		glm::vec3 collidate_hit;
		bool did_collide_hit = m_physics->GetRaycastClosestHit(start, end, collidate_hit, CollidableWorld);

		glm::vec3 non_collidate_hit;
		bool did_non_collide_hit = m_physics->GetRaycastClosestHit(start, end, non_collidate_hit, NonCollidableWorld);

		//here is where i will do selection hits but not needed yet...

		for (int i = 0; i < 5; ++i) {
			if (!process_click[i])
				continue;

			for (auto &module : m_modules) {
				if (module->GetRunning() && module->GetUnpaused()) {
					module->OnClick(i, (did_collide_hit ? &collidate_hit : nullptr), 
						(did_non_collide_hit ? &non_collidate_hit : nullptr));
				}
			}
		}
	}


	m_last_time = current_time;
}

bool Scene::TryHotkey() {
	//ImGui takes over the glfw callbacks unfortunately
	//so piggy back it for keydown events.
	auto &io = ImGui::GetIO();
	if(io.WantCaptureKeyboard) {
		return false;
	}

	bool hotkey_hit = false;
	int mods = 0;

	if(io.KeyShift) {
		mods |= GLFW_MOD_SHIFT;
	}

	if(io.KeyCtrl) {
		mods |= GLFW_MOD_CONTROL;
	}

	if(io.KeyAlt) {
		mods |= GLFW_MOD_ALT;
	}

	for(auto &hotkey : m_hotkeys) {
		if(!hotkey_hit && io.KeysDown[hotkey.key] == GLFW_RELEASE && m_key_status[hotkey.key] == GLFW_PRESS && hotkey.modifiers & mods) {
			hotkey.system->OnHotkey(hotkey.id);
			hotkey_hit = true;
		}
	}

	for(auto &hotkey : m_hotkeys) {
		m_key_status[hotkey.key] = io.KeysDown[hotkey.key];
	}

	return hotkey_hit;
}

void Scene::RegisterHotkey(SceneHotkeyListener *system, int ident, int key, bool ctrl, bool alt, bool shift) {
	if(!system) {
		return;
	}

	HotkeyEntry entry;
	entry.id = ident;
	entry.key = key;
	entry.modifiers = 0;

	if(shift)
		entry.modifiers |= GLFW_MOD_SHIFT;

	if(ctrl)
		entry.modifiers |= GLFW_MOD_CONTROL;

	if(alt)
		entry.modifiers |= GLFW_MOD_ALT;

	entry.system = system;

	m_hotkeys.push_back(entry);
}

void Scene::UnregisterHotkey(SceneHotkeyListener *system, int ident) {
	for(auto iter = m_hotkeys.begin(); iter != m_hotkeys.end(); ++iter) {
		if(iter->system == system && iter->id == ident) {
			m_hotkeys.erase(iter);
			return;
		}
	}
}

void Scene::OnHotkey(int ident) {
	switch(ident) {
		case MainHotkeyQuit:
			glfwSetWindowShouldClose(m_window, 1);
			break;
		case MainHotkeyOpen:
			m_show_open = true;
			break;
		case MainHotkeySave: {
			for(auto &module : m_modules) {
				 if(module->GetRunning() && module->GetUnpaused() && module->CanSave()) {
					 module->Save();
				 }
			}
		}
			break;
		case MainHotkeyToggleOptions:
			m_show_options = !m_show_options;
			break;
		case MainHotkeyToggleDebug:
			m_show_debug = !m_show_debug;
			break;
	}
}

void Scene::RegisterEntity(Module *m, Entity *e) {
	UnregisterEntity(m, e);

	auto iter = m_registered_entities.find(m);
	if (iter == m_registered_entities.end()) {
		std::vector<Entity*> vec;		
		m_registered_entities[m] = vec;
	}
	else {
		iter->second.push_back(e);
	}
}

void Scene::UnregisterEntity(Module *m, Entity *e) {
	auto iter = m_registered_entities.find(m);
	if (iter != m_registered_entities.end()) {
		auto &lst = iter->second;
		for(auto ent_iter = lst.begin(); ent_iter != lst.end(); ++ent_iter) {
			if((*ent_iter) == e) {
				lst.erase(ent_iter);
				return;
			}
		}	
	}
}

void Scene::UnregisterEntitiesByModule(Module *m)
{
	auto iter = m_registered_entities.find(m);
	if (iter != m_registered_entities.end()) {
		iter->second.clear();
	}
}

void Scene::RegisterModule(Module *m) {
	m->OnLoad(this);
	m_modules.push_back(std::unique_ptr<Module>(m));
}

void Scene::UnregisterModule(Module *m) {
	auto iter = m_modules.begin();
	while(iter != m_modules.end()) {
		if(m == iter->get()) {
			m->OnShutdown();
			m_modules.erase(iter);
			return;
		}

		++iter;
	}
}

void Scene::UnregisterAllModules() {
	for(auto &module : m_modules) {
		module->OnShutdown();
	}

	m_modules.clear();
}

void Scene::getClickVectors(double x, double y, glm::vec3 &start, glm::vec3 &end)
{
	glm::vec4 start_ndc(((float)x / (float)m_width - 0.5f) * 2.0f, ((float)y / (float)m_height - 0.5f) * 2.0f, -1.0, 1.0f);
	glm::vec4 end_ndc(((float)x / (float)m_width - 0.5f) * 2.0f, ((float)y / (float)m_height - 0.5f) * 2.0f, 0.0, 1.0f);
	
	glm::mat4 inverse_proj = glm::inverse(m_camera_proj);
	glm::mat4 inverse_view = glm::inverse(m_camera_view);
	
	glm::vec4 start_camera = inverse_proj * start_ndc;
	start_camera /= start_camera.w;
	
	glm::vec4 start_world = inverse_view * start_camera;
	start_world /= start_world.w;
	
	glm::vec4 end_camera = inverse_proj * end_ndc;
	end_camera /= end_camera.w;
	
	glm::vec4 end_world = inverse_view * end_camera;
	end_world /= end_world.w;
	
	glm::vec3 dir_world(end_world - start_world);
	dir_world = glm::normalize(dir_world);
	
	start = glm::vec3(start_world);
	end = glm::normalize(dir_world) * 100000.0f;
}
