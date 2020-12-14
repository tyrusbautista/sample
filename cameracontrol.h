#pragma once
#include<string>
#include<map>
#include <streamer/common/logger.h>
#include<streamer/processor/ptz/data.h>
#include<streamer/processor/ptz/preset.h>

namespace orion {
namespace streamer {
class Camera;
namespace processor {

class CameraDetails {
public:
	std::string type_;

	std::string new_abs_value_;

	std::string invert_pan_axis_;
	std::string invert_tilt_axis_;
	std::string invert_zoom_axis_;

	std::string pan_rel_factor_;
	std::string tilt_rel_factor_;
	std::string zoom_rel_factor_;

	std::string pan_abs_min_;
	std::string tilt_abs_min_;
	std::string zoom_abs_min_;

	std::string pan_abs_max_;
	std::string tilt_abs_max_;
	std::string zoom_abs_max_;

	std::string pan_min_velocity_;
	std::string tilt_min_velocity_;
	std::string zoom_min_velocity_;

	std::string pan_max_velocity_;
	std::string tilt_max_velocity_;
	std::string zoom_max_velocity_;

	std::string pan_abs_cgi_;
	std::string tilt_abs_cgi_;
	std::string zoom_abs_cgi_;
	std::string focus_abs_cgi_;
	std::string iris_abs_cgi_;

	std::string pan_rel_cgi_;
	std::string tilt_rel_cgi_;
	std::string pan_plus_cgi_;
	std::string pan_minus_cgi_;
	std::string tilt_plus_cgi_;
	std::string tilt_minus_cgi_;

	std::string ptz_position_cgi_;
	std::string use_int_values_;

	std::string pan_abs_regex_;
	std::string pan_abs_regex_val_;

	std::string tilt_abs_regex_;
	std::string tilt_abs_regex_val_;

	std::string zoom_abs_regex_;
	std::string zoom_abs_regex_val_;

	std::string ptz_pos_;

	bool load_config(const std::string& camera_name, const std::string& file);
};

class CameraControl {

public:

	typedef std::map<std::string, CameraPreset> presets_t;
	typedef std::list<std::string> preset_list_t;

	enum Type {
		TypeControlInvalid = -1,
		HttpGeneric = 0,
		Onvif,
		GpioStepper
	};

	~CameraControl();

	virtual bool control(const data_ptr_t& data) = 0;
	
	virtual void get_position(data_ptr_t& data) = 0;

	virtual bool configured();

	virtual presets_t get_presets() { return presets_; } 

	virtual preset_list_t get_preset_list() { return preset_list_; }

	void set_logger(common::Logger::logger_t logger) { logger_ = logger; }

	spdlog::logger* logger() { return (logger_.get() != nullptr)? logger_.get() : common::get_debug_logger(); }

	bool send_response() { return send_response_; }

	bool update_position() { return update_position_; }

	static std::map<Type, std::string> control_type_;

	CameraDetails details_;
protected:

	CameraControl(Camera* camera, const std::string& type, common::Logger::logger_t logger);

	Camera* camera_;

	std::string type_;

	std::string ptz_pos_;
	
	bool ready_;

	bool send_response_;
	bool send_position_;
	bool update_position_;

	presets_t presets_;
	preset_list_t preset_list_;
private:

	common::Logger::logger_t logger_;

};

}}}