#pragma once
#include <streamer/common/buffer.h>
#include <streamer/processor/ptz/cameracontrol.h>
#include <streamer/processor/ptz/ptzcontrol.h>
#include <streamer/processor/ptz/preset.h>
#include "soapDeviceBindingProxy.h"
#include "soapMediaBindingProxy.h"
#include "soapPTZBindingProxy.h"
#include "soapImagingBindingProxy.h"
#include <map>

namespace orion {
namespace streamer {
namespace processor {

class PtzControl;

class ProfileData {
public:
	int x_;
	int y_;
	int rate_limit_;
	int encoding_interval_;
	int bitrate_limit_;

	std::string codec_;
	std::string name_;
	std::string token_;
	std::string video_src_token_;

	bool abs_focus_;
	bool rel_focus_;
	bool cont_focus_;

	ProfileData()
		:x_(0)
		,y_(0)
		,rate_limit_(0)
		,encoding_interval_(0)
		,bitrate_limit_(0)
		,abs_focus_(false)
		,rel_focus_(false)
		,cont_focus_(false)
		{
		}
};

//class CameraPreset {
//public:
//	int		id_;
//	std::string name_;
//	std::string token_;
//	float		x_;
//	float		y_;
//	float		z_;
//
//	CameraPreset()
//		: id_(0)
//		, name_("")
//		, token_("")
//		, x_(0)
//		, y_(0)
//		, z_(0)
//	{
//	}
//
//	CameraPreset(int id, const char* name, const char* token, float x, float y, float z)
//		: id_(id)
//		, name_(name ? name : "")
//		, token_(token ? token : "")
//		, x_(x)
//		, y_(y)
//		, z_(z)
//	{
//	}
//};

class AxisDetails {
public:
	std::string name_;
	std::string uri_;

	float fx_min_;
	float fx_max_;
	float fy_min_;
	float fy_max_;

        AxisDetails() {}

	AxisDetails(const char* name, const char* uri, float fx_min, float fx_max, float fy_min, float fy_max)
		: name_(name ? name : "")
		, uri_(uri ? uri : "")
		, fx_min_(fx_min)
		, fx_max_(fx_max)
		, fy_min_(fy_min)
		, fy_max_(fy_max)
	{
	}
};

class PTZDetails {
public:
	std::vector<AxisDetails> ptz_axis_;
	bool home_support_;
	bool fixed_home_pos_;
	int max_preset_;

	PTZDetails(): home_support_(false), fixed_home_pos_(false), max_preset_(0)
	{
	}
};

class OnvifControl : public CameraControl{
public:
	enum Axis {
		Pan = 0,
		Tilt,
		Zoom
	};
	
	enum Status {
		Idle = 0,
		Moving,
		Unknown
	};

	OnvifControl(Camera *camera, const std::string& type, common::Logger::logger_t shared_logger);

	virtual ~OnvifControl();

	bool control(const data_ptr_t& data);
	
	void get_position(data_ptr_t& data);
protected:

private:

	void init();

	bool select_profile(ProfileData &data, const std::string& token = "");

	int set_date_and_time(const std::string& device);

	int get_capabilities(const std::string& device, const std::string& username, const std::string& password, std::string& media, std::string& ptz, std::string& imaging);

	int system_reboot(const std::string& device, const std::string& username, const std::string& password);

	int add_credential(struct soap *soap, const std::string& username, const std::string& password);

	int get_ptz_nodes(const std::string& ptz, const std::string& username, const std::string& password, std::vector<std::string>& nodes);

	int get_ptz_node(const std::string& ptz, const std::string& username, const std::string& password, const std::string& node, PTZDetails& details);

	void scale_values(const std::string& panval, const std::string& tiltval, const std::string& zoomval, float& x, float& y, float& z, float fx_min, float fx_max, float fy_min, float fy_max);

	void scale_abs_camera_values(const std::string& panval, const std::string& tiltval, const std::string& zoomval, float& x, float& y, float& z, float fx_min, float fx_max, float fy_min, float fy_max);

	void scale_abs_nvr_values(std::string& panval, std::string& tiltval, std::string& zoomval, float& x, float& y, float& z, float fx_min, float fx_max, float fy_min, float fy_max, bool zoom);

	void come_up_with_nvr_values(const char *axis, const PTZDetails& details, std::string& panval, std::string& tiltval, std::string& zoomval, float x, float y, float z, bool zoom);

	void come_up_with_camera_values(const char *axis, const PTZDetails& details, const std::string& panval, const std::string& tiltval, std::string& zoomval, float& x, float& y, float& z);

	bool come_up_with_camera_abs_values(const char *axis, const PTZDetails& details, const std::string& panval, const std::string& tiltval, const std::string& zoomval, float& x, float& y, float& z);

	bool scale_cam_rel_values(Axis axis, const PTZDetails& ptz_details, float degrees, float& value, AxisDetails& axis_details);
        
	// PTZ Commands
	int send_get_status(const std::string& ptz, const std::string& token, const std::string& username, const std::string& password, float& x, float& y, float& z, int& status);

	int send_stop(const std::string& ptz, const std::string& token, const std::string& username, const std::string& password, bool zoom);

	int send_abs_move_pt(const std::string& ptz, const std::string& token, const std::string& username, const std::string& password, float x, float y, float z);

	int send_abs_move_z(const std::string& ptz, const std::string& token, const std::string& username, const std::string& password, float x, float y, float z);

	int send_cont_move_pt(const std::string& ptz, const std::string& token, const std::string& username, const std::string& password, float x, float y, float z);

	int send_cont_move_z(const std::string& ptz, const std::string& token, const std::string& username, const std::string& password, float x, float y, float z);

	int send_relative_move_ptz(const std::string& ptz, const std::string& token, const std::string& username, const std::string& password, float x, float y, float z);

	int get_profiles(const std::string& media, const std::string& username, const std::string& password, std::vector<ProfileData> &vpd);

	int get_presets(const std::string& ptz, const std::string& profile_token, const std::string& username, const std::string& password, CameraControl::presets_t& presets);

	int set_preset(const std::string& ptz, const std::string& profile_token, std::string preset_token, std::string preset_name, const std::string& username, const std::string& password);

	int goto_preset(const std::string& ptz, const std::string& profile_token, const std::string& preset_token, const std::string& username, const std::string& password);

	int remove_preset(const std::string& ptz, const std::string& profile_token, const std::string& preset_token, const std::string& username, const std::string& password);

	int set_home_position(const std::string& ptz, const std::string& profile_token, const std::string& username, const std::string& password);

	int goto_home_position(const std::string& ptz, const std::string& profile_token, const std::string& username, const std::string& password);

	int img_get_move_options(const std::string& imaging, ProfileData& token, const std::string& username, const std::string& password);

	int img_cont_move_focus(const std::string& imaging, const std::string& token, const std::string& username, const std::string& password, float speed);

	int img_move_stop(const std::string& imaging, const std::string& token, const std::string& username, const std::string& password);
	
	int get_img_setting(const std::string& imaging, const std::string& token, const std::string& username, const std::string& password);

	void insert_port(const uint32_t& port);

	std::string convert_to_degree(float value, float min, float max, bool zoom);

	float convert_to_raw(const std::string& degree, float min, float max, bool zoom);

	bool axis_detail(const std::string& name, const PTZDetails& ptz_details, AxisDetails& axis_details);

	float calculate_range(float min, float max);

//	void update_position(Axis axis, const AxisDetails& axis_details, float addend_scaled, float addend_degree);

	bool locate_preset(std::map<std::string, CameraPreset> presets, const std::string& token, CameraPreset& preset);

	bool poll_status(int command, int16_t token = 0);
	
	void save_position(float x, float y, float z);

	void debug_ptz_node();

	std::string create_aux_url(const data_ptr_t& data);

	std::string to_str(Status status);

	std::string device_url_;
	std::string media_url_;
	std::string ptz_url_;
	std::string imaging_url_;

	float pan_raw_;
	float pan_degrees_;

	float tilt_raw_;
	float tilt_degrees_;

	float zoom_raw_;
	float zoom_degrees_;

	float pan_tilt_scale_;

	PTZDetails ptz_details_;

	bool pan_tilt_prop_;

	uint32_t status_interval_;

	std::vector<ProfileData> profiles_;
	ProfileData profile_data_;

//	std::map<std::string, CameraPreset> presets_;
};

}}}