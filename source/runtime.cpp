#include "log.hpp"
#include "version.h"
#include "runtime.hpp"
#include "runtime_objects.hpp"
#include "hook_manager.hpp"
#include "parser.hpp"
#include "preprocessor.hpp"
#include "input.hpp"
#include "string_utils.hpp"
#include "ini_file.hpp"

#include <stb_image.h>
#include <stb_image_dds.h>
#include <stb_image_write.h>
#include <stb_image_resize.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

namespace reshade
{
	namespace
	{
		filesystem::path s_executable_path, s_injector_path, s_settings_path;

		const char keyboard_keys[256][16] = {
			"", "", "", "Cancel", "", "", "", "", "Backspace", "Tab", "", "", "Clear", "Enter", "", "",
			"Shift", "Control", "Alt", "Pause", "Caps Lock", "", "", "", "", "", "", "Escape", "", "", "", "",
			"Space", "Page Up", "Page Down", "End", "Home", "Left Arrow", "Up Arrow", "Right Arrow", "Down Arrow", "Select", "", "", "Print Screen", "Insert", "Delete", "Help",
			"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "", "", "", "", "", "",
			"", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
			"P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "Left Windows", "Right Windows", "", "", "Sleep",
			"Numpad 0", "Numpad 1", "Numpad 2", "Numpad 3", "Numpad 4", "Numpad 5", "Numpad 6", "Numpad 7", "Numpad 8", "Numpad 9", "Numpad *", "Numpad +", "", "Numpad -", "Numpad Decimal", "Numpad /",
			"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16",
			"F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24", "", "", "", "", "", "", "", "",
			"Num Lock", "Scroll Lock",
		};
	}

	void runtime::startup(const filesystem::path &executable_path, const filesystem::path &injector_path)
	{
		s_injector_path = injector_path;
		s_executable_path = executable_path;

		filesystem::path log_path(injector_path), tracelog_path(injector_path);
		log_path.replace_extension("log");
		tracelog_path.replace_extension("tracelog");

		if (filesystem::exists(tracelog_path))
		{
			log::debug = true;

			log::open(tracelog_path);
		}
		else
		{
			log::open(log_path);
		}

#ifdef WIN64
#define VERSION_PLATFORM "64-bit"
#else
#define VERSION_PLATFORM "32-bit"
#endif
		LOG(INFO) << "Initializing crosire's ReShade version '" VERSION_STRING_FILE "' (" << VERSION_PLATFORM << ") built on '" VERSION_DATE " " VERSION_TIME "' loaded from " << injector_path << " to " << executable_path << " ...";

		const auto system_path = filesystem::get_special_folder_path(filesystem::special_folder::system);
		const auto appdata_path = filesystem::get_special_folder_path(filesystem::special_folder::app_data) / "ReShade";

		if (!filesystem::exists(appdata_path))
		{
			filesystem::create_directory(appdata_path);
		}

		s_settings_path = s_injector_path.parent_path() / "ReShade.ini";

		if (!filesystem::exists(s_settings_path))
		{
			s_settings_path = appdata_path / (s_executable_path.filename_without_extension() + ".ini");
		}

		hooks::register_module(system_path / "d3d8.dll");
		hooks::register_module(system_path / "d3d9.dll");
		hooks::register_module(system_path / "d3d10.dll");
		hooks::register_module(system_path / "d3d10_1.dll");
		hooks::register_module(system_path / "d3d11.dll");
		hooks::register_module(system_path / "d3d12.dll");
		hooks::register_module(system_path / "dxgi.dll");
		hooks::register_module(system_path / "opengl32.dll");
		hooks::register_module(system_path / "user32.dll");
		hooks::register_module(system_path / "ws2_32.dll");

		LOG(INFO) << "Initialized.";
	}
	void runtime::shutdown()
	{
		LOG(INFO) << "Exiting ...";

		input::uninstall();
		hooks::uninstall();

		LOG(INFO) << "Exited.";
	}

	runtime::runtime(uint32_t renderer) :
		_renderer_id(renderer),
		_start_time(std::chrono::high_resolution_clock::now()),
		_last_frame_duration(std::chrono::milliseconds(1)),
		_imgui_context(ImGui::CreateContext()),
		_effect_search_paths({ s_injector_path.parent_path() }),
		_texture_search_paths({ s_injector_path.parent_path() }),
		_preprocessor_definitions({
			"RESHADE_DEPTH_LINEARIZATION_FAR_PLANE=1000.0",
			"RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN=0",
			"RESHADE_DEPTH_INPUT_IS_REVERSED=0",
			"RESHADE_DEPTH_INPUT_IS_LOGARITHMIC=0" })
	{
		ImGui::SetCurrentContext(_imgui_context);

		auto &imgui_io = ImGui::GetIO();
		auto &imgui_style = ImGui::GetStyle();
		imgui_io.IniFilename = nullptr;
		imgui_io.KeyMap[ImGuiKey_Tab] = 0x09; // VK_TAB
		imgui_io.KeyMap[ImGuiKey_LeftArrow] = 0x25; // VK_LEFT
		imgui_io.KeyMap[ImGuiKey_RightArrow] = 0x27; // VK_RIGHT
		imgui_io.KeyMap[ImGuiKey_UpArrow] = 0x26; // VK_UP
		imgui_io.KeyMap[ImGuiKey_DownArrow] = 0x28; // VK_DOWN
		imgui_io.KeyMap[ImGuiKey_PageUp] = 0x21; // VK_PRIOR
		imgui_io.KeyMap[ImGuiKey_PageDown] = 0x22; // VK_NEXT
		imgui_io.KeyMap[ImGuiKey_Home] = 0x24; // VK_HOME
		imgui_io.KeyMap[ImGuiKey_End] = 0x23; // VK_END
		imgui_io.KeyMap[ImGuiKey_Delete] = 0x2E; // VK_DELETE
		imgui_io.KeyMap[ImGuiKey_Backspace] = 0x08; // VK_BACK
		imgui_io.KeyMap[ImGuiKey_Enter] = 0x0D; // VK_RETURN
		imgui_io.KeyMap[ImGuiKey_Escape] = 0x1B; // VK_ESCAPE
		imgui_io.KeyMap[ImGuiKey_A] = 'A';
		imgui_io.KeyMap[ImGuiKey_C] = 'C';
		imgui_io.KeyMap[ImGuiKey_V] = 'V';
		imgui_io.KeyMap[ImGuiKey_X] = 'X';
		imgui_io.KeyMap[ImGuiKey_Y] = 'Y';
		imgui_io.KeyMap[ImGuiKey_Z] = 'Z';
		imgui_style.WindowRounding = 0.0f;
		imgui_style.ChildWindowRounding = 0.0f;
		imgui_style.FrameRounding = 0.0f;
		imgui_style.ScrollbarRounding = 0.0f;
		imgui_style.GrabRounding = 0.0f;

		const auto default_settings_path = s_settings_path.parent_path() / "Defaults.ini";

		if (filesystem::exists(default_settings_path))
		{
			load_configuration(default_settings_path);
		}

		load_configuration(s_settings_path);
	}
	runtime::~runtime()
	{
		ImGui::SetCurrentContext(_imgui_context);

		ImGui::Shutdown();
		ImGui::DestroyContext(_imgui_context);

		assert(!_is_initialized && _techniques.empty());
	}

	bool runtime::on_init()
	{
		LOG(INFO) << "Recreated runtime environment on runtime " << this << ".";

		_is_initialized = true;

		reload();

		return true;
	}
	void runtime::on_reset()
	{
		on_reset_effect();

		if (!_is_initialized)
		{
			return;
		}

		_imgui_font_atlas.reset();

		LOG(INFO) << "Destroyed runtime environment on runtime " << this << ".";

		_width = _height = 0;
		_is_initialized = false;
	}
	void runtime::on_reset_effect()
	{
		_textures.clear();
		_uniforms.clear();
		_techniques.clear();
		_uniform_data_storage.clear();

		_errors.clear();
		_included_files.clear();
	}
	void runtime::on_present()
	{
		const auto time = std::time(nullptr);

		tm tm;
		localtime_s(&tm, &time);
		_date[0] = tm.tm_year + 1900;
		_date[1] = tm.tm_mon + 1;
		_date[2] = tm.tm_mday;
		_date[3] = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;

		if (!_screenshot_key_setting_active && _input->is_key_pressed(_screenshot_key.keycode, _screenshot_key.ctrl, _screenshot_key.shift, false))
		{
			save_screenshot();
		}

		draw_overlay();

		_input->next_frame();

		g_network_traffic = 0;
		_framecount++;
		_drawcalls = _vertices = 0;

		const auto ticks = std::chrono::high_resolution_clock::now();
		_last_frame_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(ticks - _last_present);
		_last_present = ticks;
	}
	void runtime::on_present_effect()
	{
		for (auto &variable : _uniforms)
		{
			const auto it = variable.annotations.find("source");

			if (it == variable.annotations.end())
			{
				continue;
			}

			const auto source = it->second.as<std::string>();

			if (source == "frametime")
			{
				const float value = _last_frame_duration.count() * 1e-6f;
				set_uniform_value(variable, &value, 1);
			}
			else if (source == "framecount" || source == "framecounter")
			{
				switch (variable.basetype)
				{
					case uniform_datatype::bool_:
					{
						const bool even = (_framecount % 2) == 0;
						set_uniform_value(variable, &even, 1);
						break;
					}
					case uniform_datatype::int_:
					case uniform_datatype::uint_:
					{
						const unsigned int framecount = static_cast<unsigned int>(_framecount % UINT_MAX);
						set_uniform_value(variable, &framecount, 1);
						break;
					}
					case uniform_datatype::float_:
					{
						const float framecount = static_cast<float>(_framecount % 16777216);
						set_uniform_value(variable, &framecount, 1);
						break;
					}
				}
			}
			else if (source == "pingpong")
			{
				float value[2] = { 0, 0 };
				get_uniform_value(variable, value, 2);

				const float min = variable.annotations["min"].as<float>(), max = variable.annotations["max"].as<float>();
				const float step_min = variable.annotations["step"].as<float>(0), step_max = variable.annotations["step"].as<float>(1);
				float increment = step_max == 0 ? step_min : (step_min + std::fmodf(static_cast<float>(std::rand()), step_max - step_min + 1));
				const float smoothing = variable.annotations["smoothing"].as<float>();

				if (value[1] >= 0)
				{
					increment = std::max(increment - std::max(0.0f, smoothing - (max - value[0])), 0.05f);
					increment *= _last_frame_duration.count() * 1e-9f;

					if ((value[0] += increment) >= max)
					{
						value[0] = max;
						value[1] = -1;
					}
				}
				else
				{
					increment = std::max(increment - std::max(0.0f, smoothing - (value[0] - min)), 0.05f);
					increment *= _last_frame_duration.count() * 1e-9f;

					if ((value[0] -= increment) <= min)
					{
						value[0] = min;
						value[1] = +1;
					}
				}

				set_uniform_value(variable, value, 2);
			}
			else if (source == "date")
			{
				set_uniform_value(variable, _date, 4);
			}
			else if (source == "timer")
			{
				const unsigned long long timer = std::chrono::duration_cast<std::chrono::nanoseconds>(_last_present - _start_time).count();

				switch (variable.basetype)
				{
					case uniform_datatype::bool_:
					{
						const bool even = (timer % 2) == 0;
						set_uniform_value(variable, &even, 1);
						break;
					}
					case uniform_datatype::int_:
					case uniform_datatype::uint_:
					{
						const unsigned int timer_int = static_cast<unsigned int>(timer % UINT_MAX);
						set_uniform_value(variable, &timer_int, 1);
						break;
					}
					case uniform_datatype::float_:
					{
						const float timer_float = std::fmod(static_cast<float>(timer * 1e-6f), 16777216.0f);
						set_uniform_value(variable, &timer_float, 1);
						break;
					}
				}
			}
			else if (source == "key")
			{
				const int key = variable.annotations["keycode"].as<int>();

				if (key > 7 && key < 256)
				{
					if (variable.annotations["toggle"].as<bool>())
					{
						bool current = false;
						get_uniform_value(variable, &current, 1);

						if (_input->is_key_pressed(key))
						{
							current = !current;

							set_uniform_value(variable, &current, 1);
						}
					}
					else
					{
						const bool state = _input->is_key_down(key);

						set_uniform_value(variable, &state, 1);
					}
				}
			}
			else if (source == "mousepoint")
			{
				const float values[2] = { static_cast<float>(_input->mouse_position_x()), static_cast<float>(_input->mouse_position_y()) };

				set_uniform_value(variable, values, 2);
			}
			else if (source == "mousebutton")
			{
				const int index = variable.annotations["keycode"].as<int>();

				if (index > 0 && index < 5)
				{
					if (variable.annotations["toggle"].as<bool>())
					{
						bool current = false;
						get_uniform_value(variable, &current, 1);

						if (_input->is_mouse_button_pressed(index))
						{
							current = !current;

							set_uniform_value(variable, &current, 1);
						}
					}
					else
					{
						const bool state = _input->is_mouse_button_down(index);

						set_uniform_value(variable, &state, 1);
					}
				}
			}
			else if (source == "random")
			{
				const int min = variable.annotations["min"].as<int>(), max = variable.annotations["max"].as<int>();
				const int value = min + (std::rand() % (max - min + 1));

				set_uniform_value(variable, &value, 1);
			}
		}

		for (auto &technique : _techniques)
		{
			if (technique.toggle_time != 0 && technique.toggle_time == _date[3])
			{
				technique.enabled = !technique.enabled;
				technique.timeleft = technique.timeout;
				technique.toggle_time = 0;
			}
			else if (technique.timeleft > 0)
			{
				technique.timeleft -= static_cast<unsigned int>(std::chrono::duration_cast<std::chrono::milliseconds>(_last_frame_duration).count());

				if (technique.timeleft <= 0)
				{
					technique.enabled = !technique.enabled;
					technique.timeleft = 0;
				}
			}
			else if (_input->is_key_pressed(technique.toggle_key, technique.toggle_key_ctrl, technique.toggle_key_shift, technique.toggle_key_alt))
			{
				technique.enabled = !technique.enabled;
				technique.timeleft = technique.timeout;
			}

			if (!technique.enabled)
			{
				technique.average_duration.clear();

				continue;
			}

			for (auto &variable : _uniforms)
			{
				const auto it = variable.annotations.find("source");

				if (it == variable.annotations.end())
				{
					continue;
				}

				const auto source = it->second.as<std::string>();

				if (source == "timeleft")
				{
					set_uniform_value(variable, &technique.timeleft, 1);
				}
			}

			const auto time_technique_started = std::chrono::high_resolution_clock::now();

			render_technique(technique);

			technique.average_duration.append(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - time_technique_started).count());
		}
	}

	void runtime::reload()
	{
		on_reset_effect();

		LOG(INFO) << "Compiling effect files ...";

		for (const auto &search_path : _effect_search_paths)
		{
			const auto files = filesystem::list_files(search_path, "*.fx");

			for (const auto &path : files)
			{
				reshadefx::syntax_tree ast;

				if (!load_effect(path, ast))
				{
					continue;
				}

				if (_performance_mode && _current_preset >= 0)
				{
					ini_file preset(_preset_files[_current_preset]);

					for (auto variable : ast.variables)
					{
						if (!variable->type.has_qualifier(reshadefx::nodes::type_node::qualifier_uniform) ||
							variable->initializer_expression == nullptr ||
							variable->initializer_expression->id != reshadefx::nodeid::literal_expression ||
							variable->annotation_list.count("source"))
						{
							continue;
						}

						const auto initializer = static_cast<reshadefx::nodes::literal_expression_node *>(variable->initializer_expression);
						const auto data = preset.get(path.filename().string(), variable->unique_name);

						for (unsigned int i = 0; i < std::min(variable->type.rows, static_cast<unsigned int>(data.data().size())); i++)
						{
							switch (initializer->type.basetype)
							{
								case reshadefx::nodes::type_node::datatype_int:
									initializer->value_int[i] = data.as<int>(i);
									break;
								case reshadefx::nodes::type_node::datatype_bool:
								case reshadefx::nodes::type_node::datatype_uint:
									initializer->value_uint[i] = data.as<unsigned int>(i);
									break;
								case reshadefx::nodes::type_node::datatype_float:
									initializer->value_float[i] = data.as<float>(i);
									break;
							}
						}

						variable->type.qualifiers ^= reshadefx::nodes::type_node::qualifier_uniform;
						variable->type.qualifiers |= reshadefx::nodes::type_node::qualifier_static | reshadefx::nodes::type_node::qualifier_const;
					}
				}

				if (!update_effect(ast, _errors))
				{
					continue;
				}

				for (auto &variable : _uniforms)
				{
					if (!variable.annotations.count("__FILE__"))
					{
						variable.annotations["__FILE__"] = path;
					}
				}
				for (auto &texture : _textures)
				{
					if (!texture.annotations.count("__FILE__"))
					{
						texture.annotations["__FILE__"] = path;
					}
				}
				for (auto &technique : _techniques)
				{
					if (!technique.annotations.count("__FILE__"))
					{
						technique.annotations["__FILE__"] = path;

						technique.enabled = technique.annotations["enabled"].as<bool>();
						technique.timeleft = technique.timeout = technique.annotations["timeout"].as<int>();
						technique.toggle_key = technique.annotations["toggle"].as<int>();
						technique.toggle_key_ctrl = technique.annotations["togglectrl"].as<bool>();
						technique.toggle_key_shift = technique.annotations["toggleshift"].as<bool>();
						technique.toggle_key_alt = technique.annotations["togglealt"].as<bool>();
						technique.toggle_time = technique.annotations["toggletime"].as<int>();
					}
				}
			}
		}

		LOG(ERROR) << "Failed to compile some effect files:\n" << _errors;

		load_textures();

		if (_current_preset >= 0)
		{
			load_preset(_preset_files[_current_preset]);
		}
	}
	bool runtime::load_effect(const filesystem::path &path, reshadefx::syntax_tree &ast)
	{
		reshadefx::parser pa(ast, _errors);
		reshadefx::preprocessor pp;

		pp.add_include_path(path.parent_path());

		for (const auto &include_path : _effect_search_paths)
		{
			if (include_path.empty())
			{
				continue;
			}

			pp.add_include_path(include_path);
		}

		pp.add_macro_definition("__RESHADE__", std::to_string(VERSION_MAJOR * 10000 + VERSION_MINOR * 100 + VERSION_REVISION));
		pp.add_macro_definition("__VENDOR__", std::to_string(_vendor_id));
		pp.add_macro_definition("__DEVICE__", std::to_string(_device_id));
		pp.add_macro_definition("__RENDERER__", std::to_string(_renderer_id));
		pp.add_macro_definition("__APPLICATION__", std::to_string(std::hash<std::string>()(s_executable_path.filename_without_extension().string())));
		pp.add_macro_definition("BUFFER_WIDTH", std::to_string(_width));
		pp.add_macro_definition("BUFFER_HEIGHT", std::to_string(_height));
		pp.add_macro_definition("BUFFER_RCP_WIDTH", std::to_string(1.0f / static_cast<float>(_width)));
		pp.add_macro_definition("BUFFER_RCP_HEIGHT", std::to_string(1.0f / static_cast<float>(_height)));

		for (const auto &definition : _preprocessor_definitions)
		{
			if (definition.empty())
			{
				continue;
			}

			const size_t equals_index = definition.find_first_of('=');

			if (equals_index != std::string::npos)
			{
				pp.add_macro_definition(definition.substr(0, equals_index), definition.substr(equals_index + 1));
			}
			else
			{
				pp.add_macro_definition(definition);
			}
		}

		if (!pp.run(path, _included_files))
		{
			_errors += pp.current_errors();

			return false;
		}

		_effect_files.push_back(path);
		_included_files.push_back(path);

		for (const auto &pragma : pp.current_pragmas())
		{
			reshadefx::lexer lexer(pragma);

			const auto prefix_token = lexer.lex();

			if (prefix_token.literal_as_string == "message")
			{
				const auto message_token = lexer.lex();

				if (message_token == reshadefx::lexer::tokenid::string_literal)
				{
					_message += message_token.literal_as_string;
				}
				continue;
			}
		}

		return pa.run(pp.current_output());
	}
	void runtime::load_textures()
	{
		LOG(INFO) << "Loading image files for textures ...";

		for (auto &texture : _textures)
		{
			if (texture.impl_is_reference)
			{
				continue;
			}

			const auto it = texture.annotations.find("source");

			if (it == texture.annotations.end())
			{
				continue;
			}

			const filesystem::path path = filesystem::resolve(it->second.as<std::string>(), _texture_search_paths);

			if (!filesystem::exists(path))
			{
				_errors += "Source '" + path.string() + "' for texture '" + texture.name + "' could not be found.";

				LOG(ERROR) << "> Source " << path << " for texture '" << texture.name << "' could not be found.";

				continue;
			}

			FILE *file;
			unsigned char *filedata = nullptr;
			int width = 0, height = 0, channels = 0;
			bool success = false;

			if (_wfopen_s(&file, stdext::utf8_to_utf16(path.string()).c_str(), L"rb") == 0)
			{
				if (stbi_dds_test_file(file))
				{
					filedata = stbi_dds_load_from_file(file, &width, &height, &channels, STBI_rgb_alpha);
				}
				else
				{
					filedata = stbi_load_from_file(file, &width, &height, &channels, STBI_rgb_alpha);
				}

				fclose(file);
			}

			if (filedata != nullptr)
			{
				if (texture.width != static_cast<unsigned int>(width) || texture.height != static_cast<unsigned int>(height))
				{
					LOG(INFO) << "> Resizing image data for texture '" << texture.name << "' from " << width << "x" << height << " to " << texture.width << "x" << texture.height << " ...";

					std::vector<uint8_t> resized(texture.width * texture.height * 4);
					stbir_resize_uint8(filedata, width, height, 0, resized.data(), texture.width, texture.height, 0, 4);
					success = update_texture(texture, resized.data());
				}
				else
				{
					success = update_texture(texture, filedata);
				}

				stbi_image_free(filedata);
			}

			if (!success)
			{
				_errors += "Unable to load source for texture '" + texture.name + "'!";

				LOG(ERROR) << "> Source " << path << " for texture '" << texture.name << "' could not be loaded! Make sure it is of a compatible file format.";
			}
		}
	}
	void runtime::load_configuration(const filesystem::path &path)
	{
		const ini_file config(path);

		const int menu_key[3] = { _menu_key.keycode, _menu_key.ctrl ? 1 : 0, _menu_key.shift ? 1 : 0 };
		_menu_key.keycode = config.get("General", "OverlayKey", menu_key).as<int>(0);
		_menu_key.ctrl = config.get("General", "OverlayKey", menu_key).as<bool>(1);
		_menu_key.shift = config.get("General", "OverlayKey", menu_key).as<bool>(2);
		_input_processing_mode = config.get("General", "InputProcessing", _input_processing_mode).as<bool>();
		_performance_mode = config.get("General", "PerformanceMode", _performance_mode).as<bool>();
		const auto effect_search_paths = config.get("General", "EffectSearchPaths", _effect_search_paths).data();
		_effect_search_paths.assign(effect_search_paths.begin(), effect_search_paths.end());
		const auto texture_search_paths = config.get("General", "TextureSearchPaths", _texture_search_paths).data();
		_texture_search_paths.assign(texture_search_paths.begin(), texture_search_paths.end());
		_preprocessor_definitions = config.get("General", "PreprocessorDefinitions", _preprocessor_definitions).data();
		const auto preset_files = config.get("General", "PresetFiles", _preset_files).data();
		_preset_files.assign(preset_files.begin(), preset_files.end());
		_current_preset = config.get("General", "CurrentPreset", _current_preset).as<int>();
		_tutorial_index = config.get("General", "TutorialProgress", _tutorial_index).as<unsigned int>();

		const int screenshot_key[3] = { _screenshot_key.keycode, _screenshot_key.ctrl ? 1 : 0, _screenshot_key.shift ? 1 : 0 };
		_screenshot_key.keycode = config.get("Screenshots", "Key", screenshot_key).as<int>(0);
		_screenshot_key.ctrl = config.get("Screenshots", "Key", screenshot_key).as<bool>(1);
		_screenshot_key.shift = config.get("Screenshots", "Key", screenshot_key).as<bool>(2);
		_screenshot_path = config.get("Screenshots", "TargetPath", s_executable_path.parent_path()).as<filesystem::path>();
		_screenshot_format = config.get("Screenshots", "ImageFormat", 0).as<int>();

		auto &style = ImGui::GetStyle();
		style.Alpha = config.get("User Interface", "Alpha", 0.95f).as<float>();

		for (size_t i = 0; i < 3; i++)
			_imgui_col_background[i] = config.get("User Interface", "ColBackground", _imgui_col_background).as<float>(i);
		for (size_t i = 0; i < 3; i++)
			_imgui_col_item_background[i] = config.get("User Interface", "ColItemBackground", _imgui_col_item_background).as<float>(i);
		for (size_t i = 0; i < 3; i++)
			_imgui_col_text[i] = config.get("User Interface", "ColText", _imgui_col_text).as<float>(i);
		for (size_t i = 0; i < 3; i++)
			_imgui_col_active[i] = config.get("User Interface", "ColActive", _imgui_col_active).as<float>(i);

		style.Colors[ImGuiCol_Text] = ImVec4(_imgui_col_text[0], _imgui_col_text[1], _imgui_col_text[2], 1.00f);
		style.Colors[ImGuiCol_TextDisabled] = ImVec4(_imgui_col_text[0], _imgui_col_text[1], _imgui_col_text[2], 0.58f);
		style.Colors[ImGuiCol_WindowBg] = ImVec4(_imgui_col_background[0], _imgui_col_background[1], _imgui_col_background[2], 1.00f);
		style.Colors[ImGuiCol_ChildWindowBg] = ImVec4(_imgui_col_item_background[0], _imgui_col_item_background[1], _imgui_col_item_background[2], 0.00f);
		style.Colors[ImGuiCol_Border] = ImVec4(_imgui_col_text[0], _imgui_col_text[1], _imgui_col_text[2], 0.30f);
		style.Colors[ImGuiCol_FrameBg] = ImVec4(_imgui_col_item_background[0], _imgui_col_item_background[1], _imgui_col_item_background[2], 1.00f);
		style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 0.68f);
		style.Colors[ImGuiCol_FrameBgActive] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 1.00f);
		style.Colors[ImGuiCol_TitleBg] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 0.45f);
		style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 0.35f);
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 0.78f);
		style.Colors[ImGuiCol_MenuBarBg] = ImVec4(_imgui_col_item_background[0], _imgui_col_item_background[1], _imgui_col_item_background[2], 0.57f);
		style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(_imgui_col_item_background[0], _imgui_col_item_background[1], _imgui_col_item_background[2], 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 0.31f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 0.78f);
		style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 1.00f);
		style.Colors[ImGuiCol_ComboBg] = ImVec4(_imgui_col_item_background[0], _imgui_col_item_background[1], _imgui_col_item_background[2], 1.00f);
		style.Colors[ImGuiCol_CheckMark] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 0.80f);
		style.Colors[ImGuiCol_SliderGrab] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 0.24f);
		style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 1.00f);
		style.Colors[ImGuiCol_Button] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 0.44f);
		style.Colors[ImGuiCol_ButtonHovered] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 0.86f);
		style.Colors[ImGuiCol_ButtonActive] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 1.00f);
		style.Colors[ImGuiCol_Header] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 0.76f);
		style.Colors[ImGuiCol_HeaderHovered] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 0.86f);
		style.Colors[ImGuiCol_HeaderActive] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 1.00f);
		style.Colors[ImGuiCol_Column] = ImVec4(_imgui_col_text[0], _imgui_col_text[1], _imgui_col_text[2], 0.32f);
		style.Colors[ImGuiCol_ColumnHovered] = ImVec4(_imgui_col_text[0], _imgui_col_text[1], _imgui_col_text[2], 0.78f);
		style.Colors[ImGuiCol_ColumnActive] = ImVec4(_imgui_col_text[0], _imgui_col_text[1], _imgui_col_text[2], 1.00f);
		style.Colors[ImGuiCol_ResizeGrip] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 0.20f);
		style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 0.78f);
		style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 1.00f);
		style.Colors[ImGuiCol_CloseButton] = ImVec4(_imgui_col_text[0], _imgui_col_text[1], _imgui_col_text[2], 0.16f);
		style.Colors[ImGuiCol_CloseButtonHovered] = ImVec4(_imgui_col_text[0], _imgui_col_text[1], _imgui_col_text[2], 0.39f);
		style.Colors[ImGuiCol_CloseButtonActive] = ImVec4(_imgui_col_text[0], _imgui_col_text[1], _imgui_col_text[2], 1.00f);
		style.Colors[ImGuiCol_PlotLines] = ImVec4(_imgui_col_text[0], _imgui_col_text[1], _imgui_col_text[2], 0.63f);
		style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 1.00f);
		style.Colors[ImGuiCol_PlotHistogram] = ImVec4(_imgui_col_text[0], _imgui_col_text[1], _imgui_col_text[2], 0.63f);
		style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 1.00f);
		style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(_imgui_col_active[0], _imgui_col_active[1], _imgui_col_active[2], 0.43f);
		style.Colors[ImGuiCol_PopupBg] = ImVec4(_imgui_col_item_background[0], _imgui_col_item_background[1], _imgui_col_item_background[2], 0.92f);

		if (_preset_files.empty())
		{
			_current_preset = -1;

			const auto preset_files = filesystem::list_files(s_injector_path.parent_path(), "*.ini", true);

			for (auto &file : preset_files)
			{
				const ini_file preset(file);

				if (preset.get("GLOBAL", "Techniques").data().empty())
				{
					continue;
				}

				_preset_files.push_back(file);
			}
		}
		else if (_current_preset >= _preset_files.size())
		{
			_current_preset = -1;
		}
	}
	void runtime::save_configuration(const filesystem::path &path) const
	{
		ini_file config(path);

		config.set("General", "OverlayKey", { _menu_key.keycode, _menu_key.ctrl ? 1 : 0, _menu_key.shift ? 1 : 0 });
		config.set("General", "InputProcessing", _input_processing_mode);
		config.set("General", "PerformanceMode", _performance_mode);
		config.set("General", "EffectSearchPaths", _effect_search_paths);
		config.set("General", "TextureSearchPaths", _texture_search_paths);
		config.set("General", "PreprocessorDefinitions", _preprocessor_definitions);
		config.set("General", "PresetFiles", _preset_files);
		config.set("General", "CurrentPreset", _current_preset);
		config.set("General", "TutorialProgress", _tutorial_index);

		config.set("Screenshots", "Key", { _screenshot_key.keycode, _screenshot_key.ctrl ? 1 : 0, _screenshot_key.shift ? 1 : 0 });
		config.set("Screenshots", "TargetPath", _screenshot_path);
		config.set("Screenshots", "ImageFormat", _screenshot_format);

		const auto &style = ImGui::GetStyle();
		config.set("User Interface", "Alpha", style.Alpha);
		config.set("User Interface", "ColBackground", _imgui_col_background);
		config.set("User Interface", "ColItemBackground", _imgui_col_item_background);
		config.set("User Interface", "ColText", _imgui_col_text);
		config.set("User Interface", "ColActive", _imgui_col_active);
	}
	void runtime::load_preset(const filesystem::path &path)
	{
		ini_file preset(path);

		for (auto &variable : _uniforms)
		{
			if (!variable.annotations.count("__FILE__"))
			{
				continue;
			}

			const std::string effect_name = variable.annotations.at("__FILE__").as<filesystem::path>().filename().string();

			float values[16] = { };
			get_uniform_value(variable, values, 16);

			const auto preset_values = preset.get(effect_name, variable.unique_name, variant(values, 16));

			for (unsigned int i = 0; i < 16; i++)
			{
				values[i] = preset_values.as<float>(i);
			}

			set_uniform_value(variable, values, 16);
		}

		// Reorder techniques
		auto order = preset.get("GLOBAL", "Techniques").data();
		std::sort(_techniques.begin(), _techniques.end(),
			[&order](const auto &lhs, const auto &rhs)
		{
			return (std::find(order.begin(), order.end(), lhs.name) - order.begin()) < (std::find(order.begin(), order.end(), rhs.name) - order.begin());
		});
		for (auto &technique : _techniques)
		{
			technique.enabled = std::find(order.begin(), order.end(), technique.name) != order.end();
		}
	}
	void runtime::save_preset(const filesystem::path &path) const
	{
		ini_file preset(path);

		for (const auto &variable : _uniforms)
		{
			if (variable.annotations.count("source") || !variable.annotations.count("__FILE__"))
			{
				continue;
			}

			const std::string effect_name = variable.annotations.at("__FILE__").as<filesystem::path>().filename().string();

			float values[16] = { };
			get_uniform_value(variable, values, 16);

			assert(variable.rows * variable.columns < 16);

			preset.set(effect_name, variable.unique_name, variant(values, variable.rows * variable.columns));
		}

		std::string technique_list;

		for (const auto &technique : _techniques)
		{
			if (technique.enabled)
			{
				technique_list += technique.name + ',';
			}
		}

		preset.set("GLOBAL", "Techniques", technique_list);
	}
	void runtime::save_screenshot()
	{
		std::vector<uint8_t> data(_width * _height * 4);
		capture_frame(data.data());

		const int hour = _date[3] / 3600;
		const int minute = (_date[3] - hour * 3600) / 60;
		const int second = _date[3] - hour * 3600 - minute * 60;

		char filename[25];
		ImFormatString(filename, sizeof(filename), " %.4d-%.2d-%.2d %.2d-%.2d-%.2d%s", _date[0], _date[1], _date[2], hour, minute, second, _screenshot_format == 0 ? ".bmp" : ".png");
		const auto path = _screenshot_path / (s_executable_path.filename_without_extension() + filename);

		LOG(INFO) << "Saving screenshot to " << path << " ...";

		FILE *file;
		bool success = false;

		if (_wfopen_s(&file, path.wstring().c_str(), L"wb") == 0)
		{
			stbi_write_func *func = [](void *context, void *data, int size)
			{
				fwrite(data, 1, size, static_cast<FILE *>(context));
			};

			switch (_screenshot_format)
			{
				case 0:
					success = stbi_write_bmp_to_func(func, file, _width, _height, 4, data.data()) != 0;
					break;
				case 1:
					success = stbi_write_png_to_func(func, file, _width, _height, 4, data.data(), 0) != 0;
					break;
			}

			fclose(file);
		}

		if (!success)
		{
			LOG(ERROR) << "Failed to write screenshot to " << path << "!";
		}
	}

	void runtime::draw_overlay()
	{
		const bool show_splash = std::chrono::duration_cast<std::chrono::seconds>(_last_present - _start_time).count() < 15;

		if (!_overlay_key_setting_active && _input->is_key_pressed(_menu_key.keycode, _menu_key.ctrl, _menu_key.shift, false))
		{
			_show_menu = !_show_menu;
		}

		if (!(_show_menu || _show_error_log || show_splash))
		{
			_input->block_mouse_input(false);
			_input->block_keyboard_input(false);
			return;
		}

		ImGui::SetCurrentContext(_imgui_context);

		auto &imgui_io = ImGui::GetIO();
		imgui_io.DeltaTime = _last_frame_duration.count() * 1e-9f;
		imgui_io.DisplaySize.x = static_cast<float>(_width);
		imgui_io.DisplaySize.y = static_cast<float>(_height);
		imgui_io.Fonts->TexID = _imgui_font_atlas.get();
		imgui_io.MouseDrawCursor = _show_menu;

		imgui_io.MousePos.x = static_cast<float>(_input->mouse_position_x());
		imgui_io.MousePos.y = static_cast<float>(_input->mouse_position_y());
		imgui_io.MouseWheel += _input->mouse_wheel_delta();
		imgui_io.KeyCtrl = _input->is_key_down(0x11); // VK_CONTROL
		imgui_io.KeyShift = _input->is_key_down(0x10); // VK_SHIFT
		imgui_io.KeyAlt = _input->is_key_down(0x12); // VK_MENU

		for (unsigned int i = 0; i < 5; i++)
		{
			imgui_io.MouseDown[i] = _input->is_mouse_button_down(i);
		}
		for (unsigned int i = 0; i < 256; i++)
		{
			imgui_io.KeysDown[i] = _input->is_key_down(i);

			if (!_input->is_key_pressed(i))
			{
				continue;
			}

			imgui_io.AddInputCharacter(_input->key_to_text(i));
		}

		ImGui::NewFrame();

		if (_input->is_key_down(0x11))
		{
			imgui_io.FontGlobalScale = ImClamp(imgui_io.FontGlobalScale + imgui_io.MouseWheel * 0.10f, 1.0f, 2.50f);
		}

		if (show_splash)
		{
			const bool has_errors = !_errors.empty();
			const ImVec2 splash_size(_width - 20.0f, ImGui::GetItemsLineHeightWithSpacing() * (has_errors ? 4 : 3));

			ImGui::Begin("Splash Screen", nullptr, splash_size, -1, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
			ImGui::SetWindowPos(ImVec2(10, 10));

			ImGui::TextUnformatted("ReShade " VERSION_STRING_FILE " by crosire");
			ImGui::TextUnformatted("Visit http://reshade.me for news, updates, shaders and discussion.");
			ImGui::Spacing();
			ImGui::Text("Press '%s%s%s' to open the configuration menu.", _menu_key.ctrl ? "Ctrl + " : "", _menu_key.shift ? "Shift + " : "", keyboard_keys[_menu_key.keycode]);

			if (has_errors)
			{
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(1, 0, 0, 1), "There were errors compiling some shaders. Open the configuration menu and click on 'Show Log' for more details.");
			}

			ImGui::End();
		}

		if (_show_menu)
		{
			ImGui::SetNextWindowSize(ImVec2(700, 600), ImGuiSetCond_Once);
			ImGui::SetNextWindowPosCenter(ImGuiSetCond_Once);
			ImGui::Begin("ReShade " VERSION_STRING_FILE " by crosire###Main", &_show_menu, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoCollapse);

			draw_overlay_menu();

			ImGui::End();
		}
		if (_show_error_log)
		{
			ImGui::SetNextWindowSize(ImVec2(500, 100), ImGuiSetCond_Once);

			if (ImGui::Begin("Error Log", &_show_error_log))
			{
				ImGui::PushTextWrapPos(0.0f);

				for (const auto &line : stdext::split(_errors, '\n'))
				{
					ImGui::TextColored(line.find("warning") != std::string::npos ? ImVec4(1, 1, 0, 1) : ImVec4(1, 0, 0, 1), line.c_str());
				}

				ImGui::PopTextWrapPos();
			}

			ImGui::End();
		}

		ImGui::Render();

		if (_input_processing_mode)
		{
			_input->block_mouse_input(imgui_io.WantCaptureMouse || (_input_processing_mode == 2 && _show_menu));
			_input->block_keyboard_input(imgui_io.WantCaptureKeyboard || (_input_processing_mode == 2 && _show_menu));
		}

		render_draw_lists(ImGui::GetDrawData());
	}
	void runtime::draw_overlay_menu()
	{
		if (ImGui::BeginMenuBar())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImGui::GetStyle().ItemSpacing * 2);

			const char *menu_items[] = { "Home", "Settings", "Statistics", "About" };

			for (int i = 0; i < 4; i++)
			{
				if (ImGui::Selectable(menu_items[i], _menu_index == i, 0, ImVec2(ImGui::CalcTextSize(menu_items[i]).x, 0)))
				{
					_menu_index = i;
				}

				ImGui::SameLine();
			}

			ImGui::PopStyleVar();

			ImGui::EndMenuBar();
		}

		switch (_menu_index)
		{
			case 0:
				draw_overlay_menu_home();
				break;
			case 1:
				draw_overlay_menu_settings();
				break;
			case 2:
				draw_overlay_menu_statistics();
				break;
			case 3:
				draw_overlay_menu_about();
				break;
		}
	}
	void runtime::draw_overlay_menu_home()
	{
		const char *tutorial_text =
			"Welcome! Since this is the first time you start ReShade, we'll go through a quick tutorial covering the most important features.\n\n"
			"Before we continue: If you have difficulties reading this text, press the 'Ctrl' key and adjust the text size with your mouse wheel. "
			"The window size is variable as well, just grab the bottom right corner and move it around.\n\n"
			"Click on the 'Continue' button to continue the tutorial.";

		if (_tutorial_index > 0)
		{
			if (_tutorial_index == 1)
			{
				tutorial_text =
					"This is the preset file selection. All changes to techniques and variables will be saved to the selected file.\n\n"
					"You can add a new one by clicking on the '+' button and entering the full path to the file. To delete the selected preset, click on the '-' button. "
					"If any valid presets were put into the same folder as ReShade (or a subdirectory), they were already added to the list for you.\n"
					"Make sure a valid file is selected here before starting to tweak any values later, or else your changes won't be saved!";

				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1, 0, 0, 1));
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 0, 0, 1));
			}

			const auto get_preset_file = [](void *data, int i, const char **out) {
				*out = static_cast<runtime *>(data)->_preset_files[i].string().c_str();
				return true;
			};

			ImGui::PushItemWidth(-(30 + ImGui::GetStyle().ItemSpacing.x) * 2 - 1);

			if (ImGui::Combo("##presets", &_current_preset, get_preset_file, this, _preset_files.size()))
			{
				save_configuration(s_settings_path);

				if (_performance_mode)
				{
					reload();
				}
				else
				{
					load_preset(_preset_files[_current_preset]);
				}
			}

			ImGui::PopItemWidth();

			ImGui::SameLine();

			if (ImGui::Button("+", ImVec2(30, 0)))
			{
				ImGui::OpenPopup("Add Preset");
			}

			if (ImGui::BeginPopup("Add Preset"))
			{
				char buf[260] = { };

				if (ImGui::InputText("Path to preset file", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
				{
					const auto path = filesystem::absolute(buf, s_injector_path);

					if (filesystem::exists(path) || filesystem::exists(path.parent_path()))
					{
						_preset_files.push_back(path);

						_current_preset = _preset_files.size() - 1;

						load_preset(path);
						save_configuration(s_settings_path);

						ImGui::CloseCurrentPopup();
					}
				}

				ImGui::EndPopup();
			}

			if (_current_preset >= 0)
			{
				ImGui::SameLine();

				if (ImGui::Button("-", ImVec2(30, 0)))
				{
					ImGui::OpenPopup("Remove Preset");
				}

				if (ImGui::BeginPopup("Remove Preset"))
				{
					ImGui::Text("Do you really want to remove this preset?");

					if (ImGui::Button("Yes", ImVec2(-1, 0)))
					{
						_preset_files.erase(_preset_files.begin() + _current_preset);

						if (_current_preset == _preset_files.size())
						{
							_current_preset -= 1;
						}
						if (_current_preset >= 0)
						{
							load_preset(_preset_files[_current_preset]);
						}

						save_configuration(s_settings_path);

						ImGui::CloseCurrentPopup();
					}

					ImGui::EndPopup();
				}
			}

			if (_tutorial_index == 1)
			{
				ImGui::PopStyleColor();
				ImGui::PopStyleColor();
			}
		}

		if (_tutorial_index > 1)
		{
			if (_tutorial_index == 2)
			{
				tutorial_text =
					"This is the list of techniques. It contains all effects (*.fx) that were found in the effect search paths as specified on the 'Settings' tab.\n\n"
					"Click on a technique to enable or disable it or drag it to a new location in the list to change the order in which the effects are applied.";

				ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, ImVec4(1, 0, 0, 1));
			}

			ImGui::Spacing();

			const float bottom_height = _performance_mode ? ImGui::GetItemsLineHeightWithSpacing() : -200;

			if (ImGui::BeginChild("##techniques", ImVec2(-1, -bottom_height), true))
			{
				draw_overlay_technique_editor();
			}

			ImGui::EndChild();

			if (_tutorial_index == 2)
			{
				ImGui::PopStyleColor();
			}
		}

		if (_tutorial_index > 2 && !_performance_mode)
		{
			if (_tutorial_index == 3)
			{
				tutorial_text =
					"This is the list of variables. It contains all tweakable options the effects expose. All values here apply in real-time.\n\n"
					"Enter text in the box at the top of the list to filter it and search for specific variable names.\n\n"
					"Once you have finished tweaking your preset, be sure to go to the 'Settings' tab and change the 'Usage Mode' to 'Performance Mode'. "
					"This will recompile all shaders into a more optimal representation that gives a significant performance boost, but will disable variable tweaking and this list.";

				ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, ImVec4(1, 0, 0, 1));
			}

			const float bottom_height = _tutorial_index == 3 ? ImGui::GetItemsLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + 120 : ImGui::GetItemsLineHeightWithSpacing();

			if (ImGui::BeginChild("##variables", ImVec2(-1, -bottom_height), true))
			{
				draw_overlay_variable_editor();
			}

			ImGui::EndChild();

			if (_tutorial_index == 3)
			{
				ImGui::PopStyleColor();
			}
		}

		if (_tutorial_index > 3)
		{
			if (ImGui::Button("Reload", ImVec2(ImGui::GetWindowContentRegionWidth() * 0.5f - 5, 0)))
			{
				reload();
			}

			ImGui::SameLine();

			if (ImGui::Button("Show Log", ImVec2(ImGui::GetWindowContentRegionWidth() * 0.5f - 5, 0)))
			{
				_show_error_log = true;
			}
		}
		else
		{
			ImGui::BeginChildFrame(0, ImVec2(-1, 120));
			ImGui::TextWrapped(tutorial_text);
			ImGui::EndChildFrame();

			if (ImGui::Button(_tutorial_index == 3 ? "Finish" : "Continue", ImVec2(-1, 0)))
			{
				_tutorial_index++;

				save_configuration(s_settings_path);
			}
		}
	}
	void runtime::draw_overlay_menu_settings()
	{
		char edit_buffer[2048];

		const auto copy_key_shortcut_to_edit_buffer = [&edit_buffer](const key_shortcut &shortcut) {
			size_t offset = 0;
			if (shortcut.ctrl) memcpy(edit_buffer, "Ctrl + ", 8), offset += 7;
			if (shortcut.shift) memcpy(edit_buffer, "Shift + ", 9), offset += 8;
			memcpy(edit_buffer + offset, keyboard_keys[shortcut.keycode], sizeof(*keyboard_keys));
		};
		const auto copy_vector_to_edit_buffer = [&edit_buffer](const std::vector<std::string> &data) {
			size_t offset = 0;
			edit_buffer[0] = '\0';
			for (const auto &line : data)
			{
				memcpy(edit_buffer + offset, line.c_str(), line.size());
				offset += line.size();
				edit_buffer[offset++] = '\n';
				edit_buffer[offset] = '\0';
			}
		};
		const auto copy_search_paths_to_edit_buffer = [&edit_buffer](const std::vector<filesystem::path> &search_paths) {
			size_t offset = 0;
			edit_buffer[0] = '\0';
			for (const auto &search_path : search_paths)
			{
				memcpy(edit_buffer + offset, search_path.string().c_str(), search_path.length());
				offset += search_path.length();
				edit_buffer[offset++] = '\n';
				edit_buffer[offset] = '\0';
			}
		};

		if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
		{
			assert(_menu_key.keycode < 256);

			copy_key_shortcut_to_edit_buffer(_menu_key);

			ImGui::InputText("Overlay Key", edit_buffer, sizeof(edit_buffer), ImGuiInputTextFlags_ReadOnly);

			_overlay_key_setting_active = false;

			if (ImGui::IsItemActive())
			{
				_overlay_key_setting_active = true;

				if (_input->is_any_key_pressed())
				{
					const unsigned int last_key_pressed = _input->last_key_pressed();

					if (last_key_pressed != 0x11 && last_key_pressed != 0x10)
					{
						_menu_key.ctrl = _input->is_key_down(0x11);
						_menu_key.shift = _input->is_key_down(0x10);
						_menu_key.keycode = _input->last_key_pressed();

						save_configuration(s_settings_path);
					}
				}
			}
			else if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Click in the field and press any key to change the shortcut to that key.");
			}

			int usage_mode_index = _performance_mode ? 0 : 1;

			if (ImGui::Combo("Usage Mode", &usage_mode_index, "Performance Mode\0Configuration Mode\0"))
			{
				_performance_mode = usage_mode_index == 0;

				save_configuration(s_settings_path);
				reload();
			}

			if (ImGui::Combo("Input Processing", &_input_processing_mode, "Pass on all input\0Block input when cursor is on overlay\0Block all input when overlay is visible\0"))
			{
				save_configuration(s_settings_path);
			}

			copy_search_paths_to_edit_buffer(_effect_search_paths);

			if (ImGui::InputTextMultiline("Effect Search Paths", edit_buffer, sizeof(edit_buffer), ImVec2(0, 60)))
			{
				const auto effect_search_paths = stdext::split(edit_buffer, '\n');
				_effect_search_paths.assign(effect_search_paths.begin(), effect_search_paths.end());

				save_configuration(s_settings_path);
			}

			copy_search_paths_to_edit_buffer(_texture_search_paths);

			if (ImGui::InputTextMultiline("Texture Search Paths", edit_buffer, sizeof(edit_buffer), ImVec2(0, 60)))
			{
				const auto texture_search_paths = stdext::split(edit_buffer, '\n');
				_texture_search_paths.assign(texture_search_paths.begin(), texture_search_paths.end());

				save_configuration(s_settings_path);
			}

			copy_vector_to_edit_buffer(_preprocessor_definitions);

			if (ImGui::InputTextMultiline("Preprocessor Definitions", edit_buffer, sizeof(edit_buffer), ImVec2(0, 100)))
			{
				_preprocessor_definitions = stdext::split(edit_buffer, '\n');

				save_configuration(s_settings_path);
			}

			if (ImGui::Button("Restart Tutorial", ImVec2(ImGui::CalcItemWidth(), 0)))
			{
				_tutorial_index = 0;
			}
		}

		if (ImGui::CollapsingHeader("Screenshots", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
		{
			assert(_screenshot_key.keycode < 256);

			copy_key_shortcut_to_edit_buffer(_screenshot_key);

			ImGui::InputText("Screenshot Key", edit_buffer, sizeof(edit_buffer), ImGuiInputTextFlags_ReadOnly);

			_screenshot_key_setting_active = false;

			if (ImGui::IsItemActive())
			{
				_screenshot_key_setting_active = true;

				if (_input->is_any_key_pressed())
				{
					const unsigned int last_key_pressed = _input->last_key_pressed();

					if (last_key_pressed != 0x11 && last_key_pressed != 0x10)
					{
						_screenshot_key.ctrl = _input->is_key_down(0x11);
						_screenshot_key.shift = _input->is_key_down(0x10);
						_screenshot_key.keycode = _input->last_key_pressed();

						save_configuration(s_settings_path);
					}
				}
			}
			else if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Click in the field and press any key to change the shortcut to that key.");
			}

			memcpy(edit_buffer, _screenshot_path.string().c_str(), _screenshot_path.length() + 1);

			if (ImGui::InputText("Screenshot Path", edit_buffer, sizeof(edit_buffer)))
			{
				_screenshot_path = edit_buffer;

				save_configuration(s_settings_path);
			}

			if (ImGui::Combo("Screenshot Format", &_screenshot_format, "Bitmap (*.bmp)\0Portable Network Graphics (*.png)\0"))
			{
				save_configuration(s_settings_path);
			}
		}

		if (ImGui::CollapsingHeader("User Interface", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
		{
			const bool modified1 = ImGui::DragFloat("Alpha", &ImGui::GetStyle().Alpha, 0.005f, 0.20f, 1.0f, "%.2f");
			const bool modified2 = ImGui::ColorEdit3("Background Color", _imgui_col_background);
			const bool modified3 = ImGui::ColorEdit3("Item Background Color", _imgui_col_item_background);
			const bool modified4 = ImGui::ColorEdit3("Text Color", _imgui_col_text);
			const bool modified5 = ImGui::ColorEdit3("Active Item Color", _imgui_col_active);

			if (modified1 || modified2 || modified3 || modified4 || modified5)
			{
				save_configuration(s_settings_path);
				load_configuration(s_settings_path);
			}
		}
	}
	void runtime::draw_overlay_menu_statistics()
	{
		if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Application: %X", std::hash<std::string>()(s_executable_path.filename_without_extension().string()));
			ImGui::Text("Date: %d-%d-%d %d", _date[0], _date[1], _date[2], _date[3]);
			ImGui::Text("Device: %X %d", _vendor_id, _device_id);
			ImGui::Text("FPS: %.2f", ImGui::GetIO().Framerate);
			ImGui::PushItemWidth(-1);
			ImGui::PlotLines("##framerate", _imgui_context->FramerateSecPerFrame, 120, _imgui_context->FramerateSecPerFrameIdx, nullptr, _imgui_context->FramerateSecPerFrameAccum / 120 * 0.5f, _imgui_context->FramerateSecPerFrameAccum / 120 * 1.5f, ImVec2(0, 50));
			ImGui::PopItemWidth();
			ImGui::Text("Draw Calls: %u (%u vertices)", _drawcalls, _vertices);
			ImGui::Text("Frame %llu: %fms", _framecount + 1, _last_frame_duration.count() * 1e-6f);
			ImGui::Text("Timer: %fms", std::fmod(std::chrono::duration_cast<std::chrono::nanoseconds>(_last_present - _start_time).count() * 1e-6f, 16777216.0f));
			ImGui::Text("Network: %uB", g_network_traffic);
		}

		if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (const auto &texture : _textures)
			{
				if (texture.impl_is_reference)
				{
					continue;
				}

				ImGui::Text("%s: %ux%u+%u (%uB)", texture.name.c_str(), texture.width, texture.height, (texture.levels - 1), (texture.width * texture.height * 4));
			}
		}

		if (ImGui::CollapsingHeader("Techniques", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (const auto &technique : _techniques)
			{
				ImGui::Text("%s (%u passes): %fms", technique.name.c_str(), static_cast<unsigned int>(technique.passes.size()), (technique.average_duration * 1e-6f));
			}
		}
	}
	void runtime::draw_overlay_menu_about()
	{
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted("\
Copyright (C) 2014 Patrick \"crosire\" Mours\n\
\n\
This software is provided 'as-is', without any express or implied warranty.\n\
In no event will the authors be held liable for any damages arising from the use of this software.\n\
\n\
Libraries in use:\n\
- MinHook\n\
  Tsuda Kageyu and contributors\n\
- gl3w\n\
  Slavomir Kaslev\n\
- dear imgui\n\
  Omar Cornut and contributors\n\
- stb_image, stb_image_write\n\
  Sean Barrett and contributors\n\
- DDS loading from SOIL\n\
  Jonathan \"lonesock\" Dummer");
		ImGui::PopTextWrapPos();
	}
	void runtime::draw_overlay_variable_editor()
	{
		ImGui::PushItemWidth(-1);

		if (ImGui::InputText("##filter", _variable_filter_buffer, sizeof(_variable_filter_buffer)))
		{
			if (_variable_filter_buffer[0] == '\0')
			{
				for (auto &uniform : _uniforms)
				{
					uniform.annotations["hidden"] = false;
				}
			}
			else
			{
				const std::string filter(_variable_filter_buffer);

				for (auto &uniform : _uniforms)
				{
					const auto filename = uniform.annotations["__FILE__"].as<std::string>();

					uniform.annotations["hidden"] =
						std::search(uniform.name.begin(), uniform.name.end(), filter.begin(), filter.end(),
						[](auto c1, auto c2) {
							return tolower(c1) == tolower(c2);
						}) == uniform.name.end() && filename.find(filter) == std::string::npos;
				}
			}
		}

		ImGui::PopItemWidth();

		ImGui::BeginChild("##variables", ImVec2(-1, -1), false, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysUseWindowPadding);

		for (int id = 0; id < _uniforms.size(); id++)
		{
			auto &variable = _uniforms[id];

			if (variable.annotations.count("source") || !variable.annotations.count("__FILE__") || variable.annotations["hidden"].as<bool>())
			{
				continue;
			}

			bool modified = false;

			const std::string filename = variable.annotations.at("__FILE__").as<filesystem::path>().filename().string();
			const auto ui_type = variable.annotations["ui_type"].as<std::string>();
			const auto ui_label = variable.annotations.count("ui_label") ? variable.annotations.at("ui_label").as<std::string>() : variable.name + " [" + filename + "]";
			const auto ui_tooltip = variable.annotations["ui_tooltip"].as<std::string>();

			ImGui::PushID(id);

			switch (variable.displaytype)
			{
				case uniform_datatype::bool_:
				{
					bool data[1] = { };
					get_uniform_value(variable, data, 1);

					int index = data[0] ? 0 : 1;

					if (ImGui::Combo(ui_label.c_str(), &index, "On\0Off\0"))
					{
						data[0] = index == 0;

						set_uniform_value(variable, data, 1);
					}
					break;
				}
				case uniform_datatype::int_:
				case uniform_datatype::uint_:
				{
					int data[4] = { };
					get_uniform_value(variable, data, 4);

					if (ui_type == "drag")
					{
						modified = ImGui::DragIntN(ui_label.c_str(), data, variable.rows, variable.annotations["ui_step"].as<int>(), variable.annotations["ui_min"].as<int>(), variable.annotations["ui_max"].as<int>(), nullptr);
					}
					else if (ui_type == "combo")
					{
						modified = ImGui::Combo(ui_label.c_str(), data, variable.annotations["ui_items"].as<std::string>().c_str());
					}
					else
					{
						modified = ImGui::InputIntN(ui_label.c_str(), data, variable.rows, 0);
					}

					if (modified)
					{
						set_uniform_value(variable, data, 4);
					}
					break;
				}
				case uniform_datatype::float_:
				{
					float data[4] = { };
					get_uniform_value(variable, data, 4);

					if (ui_type == "drag")
					{
						modified = ImGui::DragFloatN(ui_label.c_str(), data, variable.rows, variable.annotations["ui_step"].as<float>(), variable.annotations["ui_min"].as<float>(), variable.annotations["ui_max"].as<float>(), "%.3f", 1.0f);
					}
					else if (ui_type == "input" || (ui_type.empty() && variable.rows < 3))
					{
						modified = ImGui::InputFloatN(ui_label.c_str(), data, variable.rows, 8, 0);
					}
					else if (variable.rows == 3)
					{
						modified = ImGui::ColorEdit3(ui_label.c_str(), data);
					}
					else if (variable.rows == 4)
					{
						modified = ImGui::ColorEdit4(ui_label.c_str(), data);
					}

					if (modified)
					{
						set_uniform_value(variable, data, 4);
					}
					break;
				}
			}

			if (ImGui::IsItemHovered() && !ui_tooltip.empty())
			{
				ImGui::SetTooltip("%s", ui_tooltip.c_str());
			}

			ImGui::PopID();

			if (_current_preset >= 0 && modified)
			{
				save_preset(_preset_files[_current_preset]);
			}
		}

		ImGui::EndChild();
	}
	void runtime::draw_overlay_technique_editor()
	{
		int hovered_technique_index = -1;

		for (int id = 0; id < _techniques.size(); id++)
		{
			auto &technique = _techniques[id];

			if (!technique.annotations.count("__FILE__") || technique.annotations["hidden"].as<bool>())
			{
				continue;
			}

			const std::string filename = technique.annotations.at("__FILE__").as<filesystem::path>().filename().string();
			const auto ui_label = technique.name + " [" + filename + "]";

			ImGui::PushID(id);

			if (ImGui::Checkbox(ui_label.c_str(), &technique.enabled) && _current_preset >= 0)
			{
				save_preset(_preset_files[_current_preset]);
			}

			if (ImGui::IsItemActive())
			{
				_selected_technique = id;
			}
			if (ImGui::IsItemHoveredRect())
			{
				hovered_technique_index = id;
			}

			ImGui::PopID();
		}

		if (ImGui::IsMouseDragging() && _selected_technique >= 0)
		{
			ImGui::SetTooltip(_techniques[_selected_technique].name.c_str());

			if (hovered_technique_index >= 0 && hovered_technique_index != _selected_technique)
			{
				std::swap(_techniques[hovered_technique_index], _techniques[_selected_technique]);
				_selected_technique = hovered_technique_index;

				if (_current_preset >= 0)
				{
					save_preset(_preset_files[_current_preset]);
				}
			}
		}
		else
		{
			_selected_technique = -1;
		}
	}
}
