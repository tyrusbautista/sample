#include <streamer/processor/ptz/onvifcontrol.h>
#include <streamer/core/camera.h>
#include <streamer/common/utilities.h>
#include <streamer/common/string.h>
#include <math.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include "wsdd.nsmap"
#include "wsseapi.h"
//#include "wsaapi.h"
#include  <openssl/rsa.h>

namespace common = orion::streamer::common;
common::Buffer::ptr_t common::Utilities::cb_buffer_ = std::make_shared<common::Buffer>();

namespace orion {
namespace streamer {
namespace processor {
	
OnvifControl::OnvifControl(Camera *camera, const std::string& type, common::Logger::logger_t shared_logger) : status_interval_(2), CameraControl(camera, type, shared_logger)
{
	logger()->trace("OnvifControl::{} entry ", __func__);
	
	init();

	logger()->trace("OnvifControl::{} (exit)", __func__);
}

void OnvifControl::init()
{
	logger()->trace("OnvifControl::{} entry ", __func__);

	if (!ready_ && camera_ && !camera_->ptz_control_ip.empty()) {
		device_url_ = std::string("http://") + camera_->ptz_control_ip + std::string(":");
		if (camera_->ptz_control_port > 0)
			device_url_ += std::to_string(camera_->ptz_control_port);
		else
			device_url_ += "80";
		device_url_ += "/onvif/device_service";

		if (SOAP_OK == get_capabilities(device_url_, camera_->username, camera_->password, media_url_, ptz_url_, imaging_url_)) {
			// insert port to ptz and media url - usable when accessing via tunnel
			insert_port(camera_->ptz_control_port);
			if (!media_url_.empty() && SOAP_OK == get_profiles(media_url_,camera_->username, camera_->password, profiles_) && !profiles_.empty()) {
				if (select_profile(profile_data_, "")) {
					std::vector<std::string> ptz_nodes;
					if (!ptz_url_.empty() && SOAP_OK == get_ptz_nodes(ptz_url_, camera_->username, camera_->password, ptz_nodes) && !ptz_nodes.empty()) {
						if (SOAP_OK == get_ptz_node(ptz_url_, camera_->username, camera_->password, ptz_nodes[0], ptz_details_)) {
							debug_ptz_node();
							img_get_move_options(imaging_url_, profile_data_, camera_->username, camera_->password);
							this->ready_ = true;
						}
					}
				}
			}
		}
	}

	logger()->trace("OnvifControl::{} initialized = {} (exit)", __func__,  ready_ ? "True" : "False");
}	

OnvifControl::~OnvifControl()
{
	logger()->trace("OnvifControl::{} entry ", __func__);
}

bool OnvifControl::select_profile(ProfileData &data, const std::string& token /*= ""*/)
{
	logger()->trace("OnvifControl::{} token = {} (entry)", __func__, token);

	bool ret = false;

	if (!profiles_.empty()) {
		bool found = false;
		for (uint32_t i =0; i < profiles_.size(); i++) {
			if (profiles_[i].token_.compare(token)) {
				data = profiles_[i];
				found = true;
				break;
			}
		}

		if (!found)
			data = profiles_[0];		

		ret = true;
	}

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);

	return ret;
}

int OnvifControl::set_date_and_time(const std::string& device)
{
	logger()->trace("OnvifControl::{} (entry)", __func__, device);

	int ret = SOAP_ERR;

	struct soap soap;
	soap_init(&soap);
	DeviceBindingProxy proxy(&soap);

	_tds__SetSystemDateAndTime tds__SetSystemDateAndTime;
	_tds__SetSystemDateAndTimeResponse tds__SetSystemDateAndTimeResponse;

	time_t t;
	gmtime(&t);
	struct tm *dt;
	dt = gmtime(0);
	tds__SetSystemDateAndTime.UTCDateTime->Date->Year = dt->tm_year + 1900;
	tds__SetSystemDateAndTime.UTCDateTime->Date->Month = dt->tm_mon + 1;
	tds__SetSystemDateAndTime.UTCDateTime->Date->Day = dt->tm_mday;
	tds__SetSystemDateAndTime.UTCDateTime->Time->Hour = dt->tm_hour;
	tds__SetSystemDateAndTime.UTCDateTime->Time->Minute = dt->tm_min;
	tds__SetSystemDateAndTime.UTCDateTime->Time->Second = dt->tm_sec;

	ret = proxy.SetSystemDateAndTime(device.c_str(), NULL, &tds__SetSystemDateAndTime, &tds__SetSystemDateAndTimeResponse);
	if (SOAP_OK == ret)
		logger()->trace("OnvifControl::{} success", __func__);
	else
		logger()->error("OnvifControl::{} failed", __func__);

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

int OnvifControl::system_reboot(const std::string& device, const std::string& username, const std::string& password)
{
	logger()->trace("OnvifControl::{} device = {} username = {} password = {} (entry)", __func__, device, username, password);
	int ret = SOAP_ERR;

	if (!device.empty()) {
		DeviceBindingProxy proxy(device.c_str());

		_tds__SystemReboot tds__SystemReboot;
		_tds__SystemRebootResponse response;

		add_credential(proxy.soap, username, password);

		ret = proxy.SystemReboot(&tds__SystemReboot, &response);
		if (SOAP_OK == ret)
			logger()->trace("OnvifControl::{} successfully rebooted device service= {}!", __func__, device);
		else
			logger()->error("OnvifControl::{} failed to reboot device service = {}!", __func__, device);
	}

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

int OnvifControl::get_capabilities(const std::string& device, const std::string& username, const std::string& password, std::string& media, std::string& ptz, std::string& imaging)
{
	logger()->trace("OnvifControl::{} device = {} username = {} password = {} (entry)", __func__, device, username, password);
	int ret = SOAP_ERR;

	if (!device.empty()) {
		DeviceBindingProxy proxy(device.c_str());

		_tds__GetCapabilities tds__GetCapabilities;
		_tds__GetCapabilitiesResponse response;

		add_credential(proxy.soap, username, password);

		ret = proxy.GetCapabilities(&tds__GetCapabilities, &response);
		if (SOAP_OK == ret) {
			if (response.Capabilities->Media && response.Capabilities->Media->XAddr.length())
				media = response.Capabilities->Media->XAddr;
			if (response.Capabilities->PTZ && response.Capabilities->PTZ->XAddr.length())
				ptz = response.Capabilities->PTZ->XAddr;
			if (response.Capabilities->Imaging && response.Capabilities->Imaging->XAddr.length())
				imaging = response.Capabilities->Imaging->XAddr;
		} else {
			logger()->error("OnvifControl::{} failed to retrieve capabilities from device service = {}!", __func__, device);
		}
	}

	logger()->trace("OnvifControl::{} ret = {} media = {} ptz = {} imaging = {} (exit)", __func__, ret, media, ptz, imaging);
	return ret;
}

int OnvifControl::add_credential(struct soap *soap, const std::string& username, const std::string& password)
{
	int ret = SOAP_OK;

	// NOTE: soap_wsse_add_UsernameTokenDigest always return SOAP_OK
	if (!username.empty())
		ret = soap_wsse_add_UsernameTokenDigest(soap, NULL, username.c_str(), password.c_str());

	return ret;
}

int OnvifControl::get_ptz_nodes(const std::string& ptz, const std::string& username, const std::string& password, std::vector<std::string>& nodes)
{
	logger()->trace("OnvifControl::{} ptz = {} username = {} password = {} (entry)", __func__, ptz, username, password);

	int ret = SOAP_ERR;

	PTZBindingProxy proxy(ptz.c_str());

	_tptz__GetNodes tptz__GetNodes;
	_tptz__GetNodesResponse response;

	add_credential(proxy.soap, username, password);

	size_t i;
	ret = proxy.GetNodes(&tptz__GetNodes, &response);
	if (SOAP_OK == ret) {
		for (i = 0; i < response.PTZNode.size(); i++)
			nodes.push_back(response.PTZNode[i]->token);
	} else {
		logger()->error("OnvifControl::{} failed to retrieve nodes from ptz = {}!", __func__, ptz);
	}

	logger()->trace("OnvifControl::{} ret = {} nodes size = {} (exit)", __func__, ret, nodes.size());
	return ret;
}

int OnvifControl::get_ptz_node(const std::string& ptz, const std::string& username, const std::string& password, const std::string& node, PTZDetails& details)
{
	logger()->trace("OnvifControl::{} ptz = {} username = {} password = {} node = {} (entry)", __func__, ptz, username, password, node);
	int ret = SOAP_ERR;

	PTZBindingProxy proxy(ptz.c_str());

	_tptz__GetNode tptz__GetNode;
	_tptz__GetNodeResponse response;

	tptz__GetNode.NodeToken = "0";
	if (!node.empty())
		tptz__GetNode.NodeToken = node;

	add_credential(proxy.soap, username, password);

	size_t i;
	ret = proxy.GetNode(&tptz__GetNode, &response);
	if (ret == SOAP_OK) {
		if (response.PTZNode) {

			details.max_preset_ = response.PTZNode->MaximumNumberOfPresets;
			details.home_support_  = response.PTZNode->HomeSupported;
			if (response.PTZNode->FixedHomePosition)
				details.fixed_home_pos_ = *response.PTZNode->FixedHomePosition;
			
			if (response.PTZNode->SupportedPTZSpaces) {
				if (response.PTZNode->SupportedPTZSpaces->AbsolutePanTiltPositionSpace.size()) {
					for (i = 0; i < response.PTZNode->SupportedPTZSpaces->AbsolutePanTiltPositionSpace.size(); i++) {
						tt__Space2DDescription *sp = response.PTZNode->SupportedPTZSpaces->AbsolutePanTiltPositionSpace[i];
						AxisDetails ad("AbsPT",
							sp->URI.c_str(),
							sp->XRange->Min,
							sp->XRange->Max,
							sp->YRange->Min,
							sp->YRange->Max
							);
						details.ptz_axis_.push_back(ad);
					}
				}
				if (response.PTZNode->SupportedPTZSpaces->RelativePanTiltTranslationSpace.size()) {
					for (i = 0; i < response.PTZNode->SupportedPTZSpaces->RelativePanTiltTranslationSpace.size(); i++) {
						tt__Space2DDescription *sp = response.PTZNode->SupportedPTZSpaces->RelativePanTiltTranslationSpace[i];
						AxisDetails ad("RelPT",
							sp->URI.c_str(),
							sp->XRange->Min,
							sp->XRange->Max,
							sp->YRange->Min,
							sp->YRange->Max
							);
						details.ptz_axis_.push_back(ad);
					}
				}                                
				if (response.PTZNode->SupportedPTZSpaces->AbsoluteZoomPositionSpace.size()) {
					for (i = 0; i < response.PTZNode->SupportedPTZSpaces->AbsoluteZoomPositionSpace.size(); i++) {
						tt__Space1DDescription *sp = response.PTZNode->SupportedPTZSpaces->AbsoluteZoomPositionSpace[i];
						AxisDetails ad("AbsZ",
							sp->URI.c_str(),
							sp->XRange->Min,
							sp->XRange->Max,
							0,
							0
							);
						details.ptz_axis_.push_back(ad);
					}
				}
				if (response.PTZNode->SupportedPTZSpaces->RelativeZoomTranslationSpace.size()) {
					for (i = 0; i < response.PTZNode->SupportedPTZSpaces->RelativeZoomTranslationSpace.size(); i++) {
						tt__Space1DDescription *sp = response.PTZNode->SupportedPTZSpaces->RelativeZoomTranslationSpace[i];
						AxisDetails ad("RelZ",
							sp->URI.c_str(),
							sp->XRange->Min,
							sp->XRange->Max,
							0,
							0
							);
						details.ptz_axis_.push_back(ad);
					}
				}                                
				if (response.PTZNode->SupportedPTZSpaces->ContinuousPanTiltVelocitySpace.size()) {
					for (i = 0; i < response.PTZNode->SupportedPTZSpaces->ContinuousPanTiltVelocitySpace.size(); i++) {
						tt__Space2DDescription *sp = response.PTZNode->SupportedPTZSpaces->ContinuousPanTiltVelocitySpace[i];
						AxisDetails ad("ContPT",
							sp->URI.c_str(),
							sp->XRange->Min,
							sp->XRange->Max,
							sp->YRange->Min,
							sp->YRange->Max
							);
						details.ptz_axis_.push_back(ad);
					}
				}
				if (response.PTZNode->SupportedPTZSpaces->ContinuousZoomVelocitySpace.size()) {
					for (i = 0; i < response.PTZNode->SupportedPTZSpaces->ContinuousZoomVelocitySpace.size(); i++) {
						tt__Space1DDescription *sp = response.PTZNode->SupportedPTZSpaces->ContinuousZoomVelocitySpace[i];
						AxisDetails ad("ContZ",
							sp->URI.c_str(),
							sp->XRange->Min,
							sp->XRange->Max,
							0,
							0
							);
						details.ptz_axis_.push_back(ad);
					}
				}
			}
		}
	}

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

void OnvifControl::scale_values(const std::string& panval, const std::string& tiltval, const std::string& zoomval, float& x, float& y, float& z, float fx_min, float fx_max, float fy_min, float fy_max)
{
	float xrange = fx_max - fx_min;
	float yrange = fy_max - fy_min;
	float xmid = (fx_max + fx_min) / 2;
	float ymid = (fy_max + fy_min) / 2;
	if (!zoomval.empty()) {
		x = 0;
		y = 0;
		float zval = (float) atof(zoomval.c_str());
		float zr = zval / 100;
		z = xmid + (zr * xrange / 2);
	} else {
		z = 0;
		float xval = (float) atof(panval.c_str());
		float yval = (float) atof(tiltval.c_str());

		float xr = xval / 100;
		x = xmid + (xr * xrange / 2);
		float yr = yval / 100;
		y = ymid + (yr * yrange / 2);
	}
}

void OnvifControl::scale_abs_camera_values(const std::string& panval, const std::string& tiltval, const std::string& zoomval, float& x, float& y, float& z, float fx_min, float fx_max, float fy_min, float fy_max)
{
	logger()->trace("OnvifControl::{} pan = {} tilt = {}  zoom = {} (entry)", __func__, panval, tiltval, zoomval);

	if (!zoomval.empty()) {
		z = convert_to_raw(zoomval, fx_min, fx_max, true);
	} else {
		x = convert_to_raw(panval, fx_min, fx_max, false);
		y = convert_to_raw(tiltval, fy_min, fy_max, false);
	}
       
	logger()->trace("OnvifControl::{} pan = {} tilt = {} zoom = {} (exit)", __func__, x, y, z);
}
// convert degree to value
float OnvifControl::convert_to_raw(const std::string& degree, float min, float max, bool zoom)
{
	float fctr = 1.0;     /* factor */
	float v = (float)atof(degree.c_str());
	float r = v/fctr;

           
	if (zoom) {
		if (min > max) {
			float pv = (float)(r / 100.00);
			r = min - (pv * (min - max));
		} else {
			if (v != 0) {
				float pv = (float)(r / 100.00);
				r = min + (pv * (max - min));
			}
		}
	} else	{
		//-180 should equal camera min
		//+180 should equal camera max
		//changed 2012-03025  used to be
		//int mid = (iMax - iMin) / 2;
		float mid = 0;
		float frange;

		if (max < 0) {
			if (min < 0) {
				frange = min + max;
				mid = min + frange/2;
			} else {
				frange = min - max;
				//mid = (min - max) / (float)2.0;
				mid = min - frange/2;
			}
		} else {
			if (min > 0) {
				frange = max + min;
				mid = min - frange/2;
			} else {
				frange = max - min;
				//mid = (max + min) / (float)2.0;
				mid = min + frange/2;
			}
		}

//		if (v > 0) {
//			float pv = (float)(r / 180.00);
//			if (mid != 0)
//				r = mid - (pv * frange/2.0);
//			else
//				r = mid + (pv * (max - mid));
//		} else {
//			float pv = (float)(r / -180.00);
//			if (mid != 0)
//				r = mid + (pv * frange/2.0);
//			else
//				r = mid - (pv * (mid - min));
//		}

		float pv = (float)(r / 180.0);
		if (v > 0)
			r = mid + (pv * frange/2.0);
		else
			r = mid + (pv * frange/2.0);
	}

	float f = r;
//	float f = (float)floor(r + 0.5);

	if (min > max) {
		if (f < max)
			f = max;
		if (f > min)
			f = min;
	} else {
		if (f < min)
			f = min;
		if (f > max)
			f = max;
	}

	return f;
}

void OnvifControl::scale_abs_nvr_values(std::string& panval, std::string& tiltval, std::string& zoomval, float& x, float& y, float& z, float fx_min, float fx_max, float fy_min, float fy_max, bool zoom)
{
	if (zoom) {
		zoomval = convert_to_degree(z, fx_min, fx_max, zoom);
	} else {
		panval = convert_to_degree(x, fx_min, fx_max, zoom); 
		tiltval = convert_to_degree(y, fy_min, fy_max, zoom); 
	}
}

// convert camera value to degree
std::string OnvifControl::convert_to_degree(float value, float min, float max, bool zoom)
{
	float factor = 1.0;
	float r = value * factor;

	if (zoom) {
		if ((max-min) != 0) {
			float pv = (r-min) / (max - min);
			pv *= 100;
			float iv = (float)floor(pv + 0.5);
			r = (float)iv;
			if (r > 100)
				r = 100.0;
			if (r < 0)
				r = 0.0;
		}
	} else {

		float mid = 0;
		float frange;

		if (max < 0) {
			if (min < 0) {
				frange = min + max;
				mid = min + frange/2;
			} else {
				frange = min - max;
				//mid = (min - max) / (float)2.0;
				mid = min - frange/2;
			}
		} else {
			if (min > 0) {
				frange = max + min;
				mid = min - frange/2;
			} else {
				frange = max - min;
				//mid = (max + min) / (float)2.0;
				mid = min + frange/2;
			}
		}

		if (frange != 0) {
			float pv = (r-min) / (frange);
			r = pv;
			r *= 360;
			float test = value * factor;
			if (test < mid) {
				r = (float)180.0 - r;
				r = -r;
			} else {
				r -= (float)180.0;
			}
		}
	}

	return std::to_string(r);
}

void OnvifControl::come_up_with_nvr_values(const char *axis, const PTZDetails& details, std::string& panval, std::string& tiltval, std::string& zoomval, float x, float y, float z, bool zoom)
{
	size_t i;
	for (i = 0; i < details.ptz_axis_.size(); i++) {
		if (!common::Utilities::case_insensitive_compare(details.ptz_axis_[i].name_.c_str(), axis)) {
			float fx_min = 0, fx_max = 0, fy_min = 0, fy_max = 0;
			fx_min = details.ptz_axis_[i].fx_min_;
			fx_max = details.ptz_axis_[i].fx_max_;
			fy_min = details.ptz_axis_[i].fy_min_;
			fy_max = details.ptz_axis_[i].fy_max_;
			scale_abs_nvr_values(panval, tiltval, zoomval, x, y, z, fx_min, fx_max, fy_min, fy_max, zoom);
			break;
		}
	}        
}

void OnvifControl::come_up_with_camera_values(const char *axis, const PTZDetails& details, const std::string& panval, const std::string& tiltval, std::string& zoomval, float& x, float& y, float& z)
{
	size_t i;
	for (i = 0; i < details.ptz_axis_.size(); i++) {
		if (!common::Utilities::case_insensitive_compare(details.ptz_axis_[i].name_.c_str(), axis)) {
			float fx_min = 0, fx_max = 0, fy_min = 0, fy_max = 0;
			fx_min = details.ptz_axis_[i].fx_min_;
			fx_max = details.ptz_axis_[i].fx_max_;
			fy_min = details.ptz_axis_[i].fy_min_;
			fy_max = details.ptz_axis_[i].fy_max_;
			scale_values(panval, tiltval, zoomval, x, y, z, fx_min, fx_max, fy_min, fy_max);
			break;
		}
	}
}

bool  OnvifControl::come_up_with_camera_abs_values(const char *axis, const PTZDetails& details, const std::string& panval, const std::string& tiltval, const std::string& zoomval, float& x, float& y, float& z)
{
	logger()->trace("OnvifControl::{} pan = {} tilt = {}  zoom = {} (entry)", __func__, x, y, z);
	bool ret = true;

	size_t i;
	for (i = 0; i < details.ptz_axis_.size(); i++) {
		if (!common::Utilities::case_insensitive_compare(details.ptz_axis_[i].name_.c_str(), axis)) {
			float fx_min = 0, fx_max = 0, fy_min = 0, fy_max = 0;
			fx_min = details.ptz_axis_[i].fx_min_;
			fx_max = details.ptz_axis_[i].fx_max_;
			fy_min = details.ptz_axis_[i].fy_min_;
			fy_max = details.ptz_axis_[i].fy_max_;
			logger()->trace("OnvifControl::{} {} axis details x_min = {} x_max = {}  y_min = {} y_max = {}", __func__, axis, fx_min, fx_max, fy_min, fy_max);
			scale_abs_camera_values(panval, tiltval, zoomval, x, y, z, fx_min, fx_max, fy_min, fy_max);
			break;
		}
	}

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

bool OnvifControl::axis_detail(const std::string& name, const PTZDetails& ptz_details, AxisDetails& axis_details)
{
	logger()->trace("OnvifControl::{} name = {} ptz detail size = {}", __func__, name, ptz_details.ptz_axis_.size());
	bool ret = false;

	size_t i;
	for (i = 0; i < ptz_details.ptz_axis_.size(); i++) {
		if (!common::Utilities::case_insensitive_compare(ptz_details.ptz_axis_[i].name_.c_str(), name.c_str())) {
			axis_details = ptz_details.ptz_axis_[i];
			logger()->trace("OnvifControl::{} axis = {} x_min = {} x_max = {}  y_min = {} y_max = {} found",
				__func__, name, axis_details.fx_min_, axis_details.fx_max_, axis_details.fy_min_, axis_details.fy_max_);
			ret = true;
			break;
		}
	}    

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

bool OnvifControl::scale_cam_rel_values(Axis axis, const PTZDetails& ptz_details, float degrees, float& value, AxisDetails& axis_details)
{
	logger()->trace("OnvifControl::{} degrees = {} (entry)", __func__, degrees);
	
	//todo : check correct space used for scaling
	switch(axis) {
		case Axis::Zoom:
		{
			if (axis_detail("AbsZ", ptz_details, axis_details) || (axis_detail("RelZ", ptz_details, axis_details)))
				value = calculate_range(axis_details.fx_min_, axis_details.fx_max_) * (degrees/100.0);
			break;
		}
		case Axis::Pan:
		{
			if (axis_detail("AbsPT", ptz_details, axis_details) || (axis_detail("RelPT", ptz_details, axis_details)))
				value = calculate_range(axis_details.fx_min_, axis_details.fx_max_) * (degrees/360.0);
			break;
		}
		case Axis::Tilt:
		{
			if (axis_detail("AbsPT", ptz_details, axis_details) || (axis_detail("RelPT", ptz_details, axis_details)))
				value = calculate_range(axis_details.fy_min_, axis_details.fy_max_) * (degrees/360.0);
			break;
		}
		default:
			break;
	}
            
	logger()->trace("OnvifControl::{} value = {} (exit)", __func__, value);
	return (value != 0.0);
}

float OnvifControl::calculate_range(float min, float max)
{
	logger()->trace("OnvifControl::{} min = {} max = {} (entry)", __func__, min, max);
	float range = 0.0;

	if (max < 0) {
		if (min < 0)
			range = min + max;
		else
			range = min - max;
	} else {
		if (min > 0)
			range = max + min;
		else
			range = max - min;
	}

	logger()->trace("OnvifControl::{} range = {} (exit)", __func__, range);
	return range;
}

int OnvifControl::send_get_status(const std::string& ptz, const std::string& token, const std::string& username, const std::string& password, float& x, float& y, float& z, int& status)
{
	logger()->trace("OnvifControl::{} ptz = {} token = {}  username = {} password = {} (entry)", __func__, ptz, token, username, password);
	int ret = SOAP_ERR;

	PTZBindingProxy proxy(ptz.c_str());

	_tptz__GetStatus tptz__GetStatus;
	_tptz__GetStatusResponse response;

	tptz__GetStatus.ProfileToken = token;

	add_credential(proxy.soap, username, password);

	ret = proxy.GetStatus(&tptz__GetStatus, &response);
	if (SOAP_OK == ret) {
		
		x = response.PTZStatus->Position->PanTilt->x;
		y = response.PTZStatus->Position->PanTilt->y;
		z = response.PTZStatus->Position->Zoom->x;

		// IDLE = 0, MOVING = 1, UNKNOWN = 2 
		if (response.PTZStatus->MoveStatus) {
			if ((response.PTZStatus->MoveStatus->PanTilt && (Status::Moving == (Status)*response.PTZStatus->MoveStatus->PanTilt)) ||
				(response.PTZStatus->MoveStatus->Zoom && (Status::Moving == (Status)*response.PTZStatus->MoveStatus->Zoom)))
				status = Status::Moving;
			else if ((response.PTZStatus->MoveStatus->PanTilt && (Status::Unknown == (Status)*response.PTZStatus->MoveStatus->PanTilt)) ||
				(response.PTZStatus->MoveStatus->Zoom && (Status::Unknown == (Status)*response.PTZStatus->MoveStatus->Zoom)))
				status = Status::Unknown;
			else
				status = Status::Idle;
		}

		logger()->trace("OnvifControl::{} success pan = {} tilt = {} zoom = {} status = {} error = ",
			__func__, x, y, z, to_str((Status) status), (response.PTZStatus->Error ? *response.PTZStatus->Error : "None"));
	} else {
		std::string error = (response.soap && response.soap->fault && response.soap->fault->faultstring) ? response.soap->fault->faultstring : "unknown";
		logger()->error("OnvifControl::{} failed error = {}", __func__, error);
	}

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

int OnvifControl::send_stop(const std::string& ptz, const std::string& token, const std::string& username, const std::string& password, bool zoom)
{
	logger()->trace("OnvifControl::{} ptz = {} token = {}  username = {} password = {} (entry)", __func__, ptz, token, username, password);
	int ret = SOAP_ERR;

	PTZBindingProxy proxy(ptz.c_str());

	_tptz__Stop tptz__Stop;
	_tptz__StopResponse response;

	bool bt = true;
	bool bf = false;
	tptz__Stop.ProfileToken = token;

	if (zoom) {
		tptz__Stop.Zoom = &bt;
		tptz__Stop.PanTilt = &bf;
	} else {
		tptz__Stop.Zoom = &bf;
		tptz__Stop.PanTilt = &bt;
	}

	add_credential(proxy.soap, username, password);

	ret = proxy.Stop(&tptz__Stop, &response);

	if (SOAP_OK == ret)
		logger()->trace("OnvifControl::{} success zoom = {}", __func__, zoom ? "true" : "false");
	else
		logger()->error("OnvifControl::{} failed zoom = {}", __func__, zoom ? "true" : "false");

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__);
	return ret;
}

int OnvifControl::send_abs_move_pt(const std::string& ptz, const std::string& token, const std::string& username, const std::string& password, float x, float y, float z)
{
	logger()->trace("OnvifControl::{} ptz = {} token = {}  username = {} password = {} pan = {} tilt = {} (entry)", __func__, ptz, token, username, password, x, y);
	int ret = SOAP_ERR;

	PTZBindingProxy proxy(ptz.c_str());

	_tptz__AbsoluteMove tptz__AbsoluteMove;
	_tptz__AbsoluteMoveResponse response;

	tt__PTZVector v;
	tt__Vector2D vpt;
	vpt.x = x;
	vpt.y = y;
	v.PanTilt = &vpt;
	tptz__AbsoluteMove.Position = &v;
	tptz__AbsoluteMove.ProfileToken = token;

	add_credential(proxy.soap, username, password);

	ret = proxy.AbsoluteMove(&tptz__AbsoluteMove, &response);
	if (SOAP_OK == ret)
		logger()->trace("OnvifControl::{} success pan = {} tilt = {}", __func__, x, y);
	else
		logger()->error("OnvifControl::{} failed pan = {} tilt = {} error = {}", __func__, x, y, (response.soap && response.soap->fault && response.soap->fault->faultstring) ? response.soap->fault->faultstring : "unknown");

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

int OnvifControl::send_abs_move_z(const std::string& ptz, const std::string& token, const std::string& username, const std::string& password, float x, float y, float z)
{
	logger()->trace("OnvifControl::{} ptz = {} token = {}  username = {} password = {} (entry)", __func__, ptz, token, username, password);
	int ret = SOAP_ERR;

	PTZBindingProxy proxy(ptz.c_str());

	_tptz__AbsoluteMove tptz__AbsoluteMove;
	_tptz__AbsoluteMoveResponse response;

	tt__PTZVector v;
	tt__Vector1D vz;
	vz.x = z;
	v.Zoom = &vz;
	tptz__AbsoluteMove.Position = &v;
	tptz__AbsoluteMove.ProfileToken = token;

	add_credential(proxy.soap, username, password);

	ret = proxy.AbsoluteMove(&tptz__AbsoluteMove, &response);
	if (SOAP_OK == ret)
		logger()->trace("OnvifControl::{} success zval = {}", __func__, z);
	else
		logger()->error("OnvifControl::{} failed zval = {} result = {}", __func__, z, ret);

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

int OnvifControl::send_cont_move_pt(const std::string& ptz, const std::string& token, const std::string& username, const std::string& password, float x, float y, float z)
{
	logger()->trace("OnvifControl::{} ptz = {} token = {}  username = {} password = {} (entry)", __func__, ptz, token, username, password);
	int ret = SOAP_ERR;

	PTZBindingProxy proxy(ptz.c_str());

	_tptz__ContinuousMove tptz__ContinuousMove;
	_tptz__ContinuousMoveResponse response;

	tt__PTZSpeed v;
	tt__Vector2D vpt;

	if (pan_tilt_scale_ != 0.0) {
		vpt.x = x * pan_tilt_scale_;
		vpt.y = y * pan_tilt_scale_;
	} else {
		vpt.x = x;
		vpt.y = y;
	}
	//handle proportional speed base on zoom value
	if (pan_tilt_prop_ && zoom_degrees_ != 0) {
		float sc = (float) (1.0 - (zoom_degrees_ / 100.0));
		if (sc == 0.0)
			sc = (float) 0.05;
		vpt.x = vpt.x * sc;
		vpt.y = vpt.y * sc;
	}

	v.PanTilt = &vpt;
	tptz__ContinuousMove.Velocity = &v;
	tptz__ContinuousMove.ProfileToken = token;

	add_credential(proxy.soap, username, password);

	ret = proxy.ContinuousMove(&tptz__ContinuousMove, &response);
	if (SOAP_OK == ret)
		logger()->trace("OnvifControl::{} success  xval = {} yval = {}", __func__, x, y);
	else
		logger()->error("OnvifControl::{} failed  xval = {} yval = {}", __func__, x, y);

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

int OnvifControl::send_cont_move_z(const std::string& ptz, const std::string& token, const std::string& username, const std::string& password, float x, float y, float z)
{
	logger()->trace("OnvifControl::{} ptz = {} token = {}  username = {} password = {} (entry)", __func__, ptz, token, username, password);
	int ret = SOAP_ERR;

	PTZBindingProxy proxy(ptz.c_str());

	_tptz__ContinuousMove tptz__ContinuousMove;
	_tptz__ContinuousMoveResponse response;

	tt__PTZSpeed v;
	tt__Vector1D vz;
	vz.x = z;
	v.Zoom = &vz;
	tptz__ContinuousMove.Velocity = &v;
	tptz__ContinuousMove.ProfileToken = token;

	add_credential(proxy.soap, username, password);

	ret = proxy.ContinuousMove(&tptz__ContinuousMove, &response);
	if (SOAP_OK == ret)
		logger()->trace("OnvifControl::{} success  zval = {}", __func__, z);
	else
		logger()->error("OnvifControl::{} failed  zval = {}", __func__, z);

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__);
	return ret;
}

void OnvifControl::get_position(data_ptr_t& data)
{
	logger()->trace("OnvifControl::{} (entry)", __func__);

	// Update PTZ position when needed
	if (update_position()) {
		data_ptr_t pos_data = std::make_shared<Data>(*data);
		pos_data->type = (uint8_t) PtzControl::Type::GetPanTiltZoomPos;
		control(pos_data);
	}

	logger()->trace("OnvifControl::{} RAW camera values pan = {} tilt = {} zoom = {}", __func__, pan_raw_, tilt_raw_, zoom_raw_);
	logger()->trace("OnvifControl::{} NVR values pan = {} tilt = {} zoom = {}", __func__, pan_degrees_, tilt_degrees_, zoom_degrees_);

	data->pan = (int16_t) pan_degrees_;
	data->tilt = (int16_t) tilt_degrees_;
	data->zoom = (int16_t) zoom_degrees_;
	
	logger()->trace("OnvifControl::{} (exit)", __func__);
}

void OnvifControl::insert_port(const uint32_t& port)
{
	logger()->trace("OnvifControl::{} port = {} (entry)", __func__, port);

	if (port > 0) {
		std::string insert = std::string(":") + std::to_string(port);
		std::size_t pos_media = media_url_.find("/onvif");

		if (std::string::npos != pos_media)
			media_url_.insert(pos_media, insert);

		std::size_t pos_ptz = ptz_url_.find("/onvif");
		if (std::string::npos != pos_ptz)
			ptz_url_.insert(pos_ptz, insert);

		std::size_t pos_imaging = imaging_url_.find("/onvif");
		if (std::string::npos != pos_imaging)
			imaging_url_.insert(pos_imaging, insert);

		logger()->trace("OnvifControl::{} updated media url = {} ptz url = {} imaging url = {} (entry)", __func__, media_url_, ptz_url_, imaging_url_);
	}

	logger()->trace("OnvifControl::{} (exit)", __func__);
}

int OnvifControl::send_relative_move_ptz(const std::string& ptz, const std::string& token, const std::string& username, const std::string& password, float x, float y, float z)
{
	logger()->trace("OnvifControl::{} ptz = {} token = {}  username = {} password = {} pan = {} tilt = {} zoom = {} (entry)", __func__, ptz, token, username, password, x, y, z);
	int ret = SOAP_ERR;

	PTZBindingProxy proxy(ptz.c_str());

	_tptz__RelativeMove tptz__RelativeMove;
	_tptz__RelativeMoveResponse response;

	tt__PTZVector v;
	tt__Vector2D vpt;
	tt__Vector1D vz;

	vpt.x = x;
	vpt.y = y;
	vz.x = z;
	v.PanTilt = &vpt;
	v.Zoom = &vz;
	tptz__RelativeMove.Translation = &v;
	tptz__RelativeMove.ProfileToken = token;

	add_credential(proxy.soap, username, password);

	ret = proxy.RelativeMove(&tptz__RelativeMove, &response);
	if (SOAP_OK == ret)
		logger()->trace("OnvifControl::{} success pan = {} tilt = {} zoom = {}", __func__, x, y, z);
	else
		logger()->error("OnvifControl::{} failed pan = {} tilt = {} zoom = {} error = {}", __func__, x, y, z, 
			(response.soap && response.soap->fault && response.soap->fault->faultstring) ? response.soap->fault->faultstring : "unknown");

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

int OnvifControl::get_profiles(const std::string& media, const std::string& username, const std::string& password, std::vector<ProfileData> &vpd)
{
	logger()->trace("OnvifControl::{} media = {} username = {} password = {} (entry)", __func__, media, username, password);

	int ret = SOAP_ERR;

	std::map<std::string, std::string> profiles;

	MediaBindingProxy proxy(media.c_str());

	_trt__GetProfiles trt__GetProfiles;
	_trt__GetProfilesResponse trt__GetProfilesResponse;

	add_credential(proxy.soap, username, password);

	ret = proxy.GetProfiles(&trt__GetProfiles, &trt__GetProfilesResponse);
	if (SOAP_OK == ret) {
		for (std::vector<tt__Profile * >::const_iterator it = trt__GetProfilesResponse.Profiles.begin(); it != trt__GetProfilesResponse.Profiles.end(); ++it) {
			tt__Profile* profile = *it;
			ProfileData profile_data;
			profiles[profile->token] = profile->Name;

			profile_data.name_ = profile->Name;
			profile_data.token_ = profile->token;

			logger()->trace("OnvifControl::{} profile {}: {} - fixed: {}", __func__, profile->token.c_str(), profile->Name.c_str(), *(profile->fixed));

			if (profile->VideoSourceConfiguration) {
				profile_data.video_src_token_ = profile->VideoSourceConfiguration->SourceToken;
				logger()->trace("OnvifControl::{} video source token = {}", __func__, profile_data.video_src_token_);
			}

			if (profile->VideoEncoderConfiguration) {
				std::string encoding;
				switch (profile->VideoEncoderConfiguration->Encoding) {
				case tt__VideoEncoding__JPEG: encoding = "JPEG";
					break;
				case tt__VideoEncoding__MPEG4: encoding = "MPEG4";
					break;
				case tt__VideoEncoding__H264: encoding = "H264";
					break;
				default: break;
				}

				profile_data.codec_ = encoding;
				profile_data.x_ = profile->VideoEncoderConfiguration->Resolution->Width;
				profile_data.y_ = profile->VideoEncoderConfiguration->Resolution->Height;

				logger()->trace("OnvifControl::{} codec = {} resolution = {}x{}", __func__, encoding, profile_data.x_, profile_data.y_);

				if (profile->VideoEncoderConfiguration->RateControl) {
					profile_data.rate_limit_ = profile->VideoEncoderConfiguration->RateControl->FrameRateLimit;
					profile_data.encoding_interval_ = profile->VideoEncoderConfiguration->RateControl->EncodingInterval;
					profile_data.bitrate_limit_ = profile->VideoEncoderConfiguration->RateControl->BitrateLimit;
					logger()->trace("OnvifControl::{} FPS limit = {} encoding interval = {} bitrate limit = {}", __func__, profile_data.rate_limit_, profile_data.encoding_interval_, profile_data.bitrate_limit_);
				}

				// Todo add loading of optional configuratio ptz limits 
				if (profile->PTZConfiguration) {
				}
                                
				vpd.push_back(profile_data);
			}
		}
	} else {
		logger()->error("OnvifControl::{} failed  error = {}", __func__, proxy.soap_fault_detail());
	}

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}


int OnvifControl::get_presets(const std::string& ptz, const std::string& profile_token, const std::string& username, const std::string& password, CameraControl::presets_t& presets)
{
	logger()->trace("OnvifControl::{} ptz = {} profile token = {} username = {} password = {} (entry)", __func__, ptz, profile_token, username, password);
	int ret = SOAP_ERR;

	PTZBindingProxy proxy(ptz.c_str());

	_tptz__GetPresets tptz__GetPresets;
	_tptz__GetPresetsResponse response;

	tptz__GetPresets.ProfileToken = profile_token;

	add_credential(proxy.soap, username, password);

	ret = proxy.GetPresets(&tptz__GetPresets, &response);
	if (SOAP_OK == ret) {
		logger()->trace("OnvifControl::{} success", __func__);
		presets.clear();
		for (uint32_t i = 0; i < response.Preset.size(); i++) {
			logger()->trace("OnvifControl::{} Adding preset = {}", __func__, *response.Preset[i]->token  );
			CameraPreset preset(i,
				response.Preset[i]->Name->c_str(),
				response.Preset[i]->token->c_str(),
				response.Preset[i]->PTZPosition->PanTilt->x,
				response.Preset[i]->PTZPosition->PanTilt->y,
				response.Preset[i]->PTZPosition->Zoom->x);
			presets[*response.Preset[i]->token] = preset;
			preset_list_.push_back(*response.Preset[i]->token);
		}
			
	} else {
		logger()->error("OnvifControl::{} failed error = {}!", __func__,  
			(response.soap && response.soap->fault && response.soap->fault->faultstring) ? response.soap->fault->faultstring : "unknown");
	}

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

int OnvifControl::set_preset(const std::string& ptz, const std::string& profile_token,
	std::string preset_token, std::string preset_name, const std::string& username, const std::string& password)
{
	logger()->trace("OnvifControl::{} ptz = {} profile token = {}  preset token = {}  preset name = {} username = {} password = {} (entry)", __func__, ptz, profile_token, preset_token, preset_name, username, password);
	int ret = SOAP_ERR;

	PTZBindingProxy proxy(ptz.c_str());

	_tptz__SetPreset tptz__SetPreset;
	_tptz__SetPresetResponse response;

	tptz__SetPreset.ProfileToken = profile_token;
	tptz__SetPreset.PresetToken = &preset_token;
	tptz__SetPreset.PresetName = &preset_name;

	add_credential(proxy.soap, username, password);

	ret = proxy.SetPreset(&tptz__SetPreset, &response);
	if (SOAP_OK == ret)
		logger()->trace("OnvifControl::{} successfully created preset token = {} name = {} in profile = {}", __func__, preset_token, preset_name, profile_token);
	else
		logger()->error("OnvifControl::{} failed error = {}!", __func__,  
			(response.soap && response.soap->fault && response.soap->fault->faultstring) ? response.soap->fault->faultstring : "unknown");

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

int OnvifControl::goto_preset(const std::string& ptz, const std::string& profile_token,
	const std::string& preset_token, const std::string& username, const std::string& password)
{
	logger()->trace("OnvifControl::{} ptz = {} profile token = {} preset token = {}  username = {} password = {} (entry)", __func__, ptz, profile_token, preset_token, username, password);
	int ret = SOAP_ERR;

	PTZBindingProxy proxy(ptz.c_str());

	_tptz__GotoPreset tptz__GotoPreset;
	_tptz__GotoPresetResponse response;

	tptz__GotoPreset.ProfileToken = profile_token;
	tptz__GotoPreset.PresetToken = preset_token;

	add_credential(proxy.soap, username, password);

	ret = proxy.GotoPreset(&tptz__GotoPreset, &response);
	if (SOAP_OK == ret)
		logger()->trace("OnvifControl::{} success", __func__);
	else
		logger()->error("OnvifControl::{} failed error = {}!", __func__,  
			(response.soap && response.soap->fault && response.soap->fault->faultstring) ? response.soap->fault->faultstring : "unknown");

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

int OnvifControl::remove_preset(const std::string& ptz, const std::string& profile_token,
	const std::string& preset_token, const std::string& username, const std::string& password)
{
	logger()->trace("OnvifControl::{} ptz = {} profile token = {}  preset token = {}  username = {} password = {} (entry)", __func__, ptz, profile_token, preset_token, username, password);
	int ret = SOAP_ERR;

	PTZBindingProxy proxy(ptz.c_str());

	_tptz__RemovePreset tptz__RemovePreset;
	_tptz__RemovePresetResponse response;

	tptz__RemovePreset.ProfileToken = profile_token;
	tptz__RemovePreset.PresetToken = preset_token;

	add_credential(proxy.soap, username, password);

	ret = proxy.RemovePreset(&tptz__RemovePreset, &response);
	if (SOAP_OK == ret)
		logger()->trace("OnvifControl::{} success", __func__);
	else
		logger()->error("OnvifControl::{} failed error = {}!", __func__,  
			(response.soap && response.soap->fault && response.soap->fault->faultstring) ? response.soap->fault->faultstring : "unknown");

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

int OnvifControl::set_home_position(const std::string& ptz, const std::string& profile_token, const std::string& username, const std::string& password)
{
	logger()->trace("OnvifControl::{} ptz = {} profile token = {} username = {} password = {} (entry)", __func__, ptz, profile_token, username, password);
	int ret = SOAP_ERR;

	PTZBindingProxy proxy(ptz.c_str());

	_tptz__SetHomePosition tptz__SetHomePosition;
	_tptz__SetHomePositionResponse response;

	tptz__SetHomePosition.ProfileToken = profile_token;

	add_credential(proxy.soap, username, password);

	ret = proxy.SetHomePosition(&tptz__SetHomePosition, &response);
	if (SOAP_OK == ret)
		logger()->trace("OnvifControl::{} success", __func__);
	else
		logger()->error("OnvifControl::{} failed error = {}!", __func__,  
			(response.soap && response.soap->fault && response.soap->fault->faultstring) ? response.soap->fault->faultstring : "unknown");

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

int OnvifControl::goto_home_position(const std::string& ptz, const std::string& profile_token, const std::string& username, const std::string& password)
{
	logger()->trace("OnvifControl::{} ptz = {} profile token = {} username = {} password = {} (entry)", __func__, ptz, profile_token, username, password);
	int ret = SOAP_ERR;

	PTZBindingProxy proxy(ptz.c_str());

	_tptz__GotoHomePosition tptz__GotoHomePosition;
	_tptz__GotoHomePositionResponse response;

	tptz__GotoHomePosition.ProfileToken = profile_token;

	add_credential(proxy.soap, username, password);

	ret = proxy.GotoHomePosition(&tptz__GotoHomePosition, &response);
	if (SOAP_OK == ret)
		logger()->trace("OnvifControl::{} success", __func__);
	else
		logger()->error("OnvifControl::{} failed error = {}!", __func__,  
			(response.soap && response.soap->fault && response.soap->fault->faultstring) ? response.soap->fault->faultstring : "unknown");

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

bool OnvifControl::locate_preset(std::map<std::string, CameraPreset> presets, const std::string& token, CameraPreset& preset)
{
	logger()->trace("OnvifControl::{} token = {} (entry)", __func__, token);
	bool ret = false;

	if (!token.empty()) {
		std::map<std::string, CameraPreset>::iterator it = presets.find(token);
		if (presets.end() != it) {
			ret = true;
			preset = it->second;
		}
	}

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

void OnvifControl::save_position(float x, float y, float z)
{
	std::string pan, tilt, zoom;

	come_up_with_nvr_values("AbsPT", ptz_details_, pan, tilt, zoom, x, y, z, false);
	come_up_with_nvr_values("AbsZ", ptz_details_, pan, tilt, zoom, x, y, z, true);

	// NVR values in degrees
	
	pan_degrees_ = std::stof(pan);
	tilt_degrees_ = std::stof(tilt);
	zoom_degrees_ = std::stof(zoom);
	
	// RAW camera values
	pan_raw_ = x;
	tilt_raw_ = y;
	zoom_raw_ = z;

}

bool OnvifControl::poll_status(int command, int16_t token /*= 0*/)
{
	logger()->trace("OnvifControl::{} (entry)", __func__);
	bool ret = false;	

	switch((PtzControl::Type) command) {
		case PtzControl::Type::Pan:
		case PtzControl::Type::Tilt:
		case PtzControl::Type::Zoom:
		case PtzControl::Type::PanTilt:
		case PtzControl::Type::PanTiltZoom:
		case PtzControl::Type::PanAbs:
		case PtzControl::Type::TiltAbs:
		case PtzControl::Type::ZoomAbs:
		case PtzControl::Type::PanTiltAbs:
		case PtzControl::Type::PanTiltZoomAbs:
		case PtzControl::Type::Focus:
		case PtzControl::Type::PanPlus:
		case PtzControl::Type::PanMinus:
		case PtzControl::Type::TiltPlus:
		case PtzControl::Type::TiltMinus:
		case PtzControl::Type::ZoomPlus:
		case PtzControl::Type::ZoomMinus:
		case PtzControl::Type::GotoPreset:
		case PtzControl::Type::GotoHomePosition:
		case PtzControl::Type::SelectiveZoom:
		{
			bool loop = true;
			int ctr = 0;
			do{
				int status = 0;
				float x = 0, y = 0, z = 0;
				sleep(status_interval_);
				if (SOAP_OK == send_get_status(ptz_url_, profile_data_.token_, camera_->username, camera_->password, x, y, z, status)) {
					if (Status::Idle == status) {
						save_position(x, y, z);
						loop = false;
						ret = true;
					} else {
						logger()->trace("OnvifControl::{} device status = {}, checking again after 2 seconds", __func__, to_str((Status) status));
					}
				} else {
					loop = false;
				}

				ctr++;
			} while (loop && ctr < 3);

			break;
		}
		case PtzControl::Type::SetPreset:
		{
			// Check if we successfully added the preset
			if (token) {
				if (SOAP_OK == get_presets(ptz_url_, profile_data_.token_, camera_->username, camera_->password, presets_)) {
					CameraPreset preset;
					std::string preset_token = std::to_string(token);
					if (locate_preset(presets_, preset_token, preset))
						ret = true;
				}
			}
			break;
		}
		case PtzControl::Type::GetPresets:
		case PtzControl::Type::SetHomePosition:
		case PtzControl::Type::PanTiltZoomReset:
		case PtzControl::Type::FocusNear:
		case PtzControl::Type::FocusFar:
		case PtzControl::Type::FocusStop:	
			break;
		default:
			break;
	}

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

bool OnvifControl::control(const data_ptr_t& data)
{
	logger()->trace("OnvifControl::{} initialized = {} (entry)", __func__, ready_ ? "True" : "False");
	int ret = SOAP_ERR;	

	if (!ready_)
		init();

	if (ready_ && data.get()) {
		logger()->trace("OnvifControl::{} processing command type = {} ", __func__, PtzControl::to_str((PtzControl::Type) data->type));

		send_response_ = false;
		update_position_ = false;

		float x = 0, y = 0, z = 0;
		std::string pan, tilt, zoom;
		int status = 0;
		switch((PtzControl::Type) data->type) {
			case PtzControl::Type::GetPanTiltZoomPos:
			{
				ret = send_get_status(ptz_url_, profile_data_.token_, camera_->username, camera_->password, x, y, z, status);
				if (SOAP_OK == ret) {
					save_position(x, y, z);
					update_position_ = false;
					send_response_ = true;
					logger()->trace("OnvifControl::{} command = {} pan = {} tilt = {} zoom = {}", __func__,
						PtzControl::commands_[PtzControl::Type::GetPanTiltZoomPos], pan_degrees_, tilt_degrees_, zoom_degrees_);
				}
				break;
			}
			case PtzControl::Type::PanAbs:
			{
				pan = std::to_string(data->pan);
				if (come_up_with_camera_abs_values("AbsPT", ptz_details_, pan, tilt, zoom, x, y, z)) {
					ret = send_abs_move_pt(ptz_url_, profile_data_.token_, camera_->username, camera_->password, x, tilt_raw_, 0);
					if (SOAP_OK == ret) {
						send_response_ = true;
						update_position_ = poll_status(PtzControl::Type::PanAbs) ? false : true;
					}
				} 
				break;
			}
			case PtzControl::Type::TiltAbs:
			{
				tilt = std::to_string(data->tilt);
				if (come_up_with_camera_abs_values("AbsPT", ptz_details_, pan, tilt, zoom, x, y, z)) {
					ret = send_abs_move_pt(ptz_url_, profile_data_.token_, camera_->username, camera_->password, pan_raw_, y, 0);
					if (SOAP_OK == ret) {
						send_response_ = true;
						update_position_ = poll_status(PtzControl::Type::TiltAbs) ? false : true;
					}
				}  
				break;
			}
			case PtzControl::Type::ZoomAbs:
			{
				zoom = std::to_string(data->zoom);
				if (come_up_with_camera_abs_values("AbsZ", ptz_details_, pan, tilt, zoom, x, y, z)) {
					ret = send_abs_move_z(ptz_url_, profile_data_.token_, camera_->username, camera_->password, 0, 0, z);
					if (SOAP_OK == ret) {
						send_response_ = true;
						update_position_ = poll_status(PtzControl::Type::ZoomAbs) ? false : true;
					}
				}
				break;
			}
			case PtzControl::Type::Pan:
			{
				float pan_scaled = 0.0; 
				AxisDetails axis_details;
				if ((data->pan != 0.0) && scale_cam_rel_values(Axis::Pan, ptz_details_, data->pan, pan_scaled, axis_details)) {
					ret = send_relative_move_ptz(ptz_url_, profile_data_.token_, camera_->username, camera_->password, pan_scaled, 0, 0);
					if (SOAP_OK == ret) {
						send_response_ = true;
						update_position_ = poll_status(PtzControl::Type::Pan) ? false : true;
					}
				}				
				break;
			}
			case PtzControl::Type::PanPlus:
			{
				float pan_scaled = 0.0; 
				float pan_degree = 10.0;
				AxisDetails axis_details;
				if (scale_cam_rel_values(Axis::Pan, ptz_details_, pan_degree, pan_scaled, axis_details)) {
					ret = send_relative_move_ptz(ptz_url_, profile_data_.token_, camera_->username, camera_->password, pan_scaled, 0, 0);
					if (SOAP_OK == ret) {
						send_response_ = true;
						update_position_ = poll_status(PtzControl::Type::PanPlus) ? false : true;
					}
				}
				break;
			}
			case PtzControl::Type::PanMinus:
			{
				float pan_scaled = 0.0; 
				float pan_degree = -10.0;
				AxisDetails axis_details;
				if (scale_cam_rel_values(Axis::Pan, ptz_details_, pan_degree, pan_scaled, axis_details)) {
					ret = send_relative_move_ptz(ptz_url_, profile_data_.token_, camera_->username, camera_->password, pan_scaled, 0, 0);
					if (SOAP_OK == ret) {
						send_response_ = true;
						update_position_ = poll_status(PtzControl::Type::PanMinus) ? false : true;
					}
				}	
				break;
			}
			case PtzControl::Type::Tilt:
			{
				float tilt_scaled = 0.0; 
				AxisDetails axis_details;
				if ((data->tilt != 0.0) && scale_cam_rel_values(Axis::Tilt, ptz_details_, data->tilt, tilt_scaled, axis_details) ) {
					ret = send_relative_move_ptz(ptz_url_, profile_data_.token_, camera_->username, camera_->password, 0, tilt_scaled, 0);
					if (SOAP_OK == ret) {
						send_response_ = true;
						update_position_ = poll_status(PtzControl::Type::Tilt) ? false : true;
					}
				}				
				break;
			}
			case PtzControl::Type::TiltPlus:
			{
				float tilt_scaled = 0.0; 
				float tilt_degree = 10.0;
				AxisDetails axis_details;
				if (scale_cam_rel_values(Axis::Tilt, ptz_details_, tilt_degree, tilt_scaled, axis_details)) {
					ret = send_relative_move_ptz(ptz_url_, profile_data_.token_, camera_->username, camera_->password, 0, tilt_scaled, 0);
					if (SOAP_OK == ret) {
						send_response_ = true;
						update_position_ = poll_status(PtzControl::Type::TiltPlus) ? false : true;
					}
				}
				break;
			}
			case PtzControl::Type::TiltMinus:
			{
				float tilt_scaled = 0.0; 
				float tilt_degree = -10.0;
				AxisDetails axis_details;
				if (scale_cam_rel_values(Axis::Tilt, ptz_details_, tilt_degree, tilt_scaled, axis_details)) {
					ret = send_relative_move_ptz(ptz_url_, profile_data_.token_, camera_->username, camera_->password, 0, tilt_scaled, 0);
					if (SOAP_OK == ret) {
						send_response_ = true;
						update_position_ = poll_status(PtzControl::Type::TiltMinus) ? false : true;
					}
				}
				break;
			}
			case PtzControl::Type::Zoom:
			{
				float zoom_scaled = 0.0; 
				AxisDetails axis_details;
				if ((data->zoom != 0.0) && scale_cam_rel_values(Axis::Zoom, ptz_details_, data->zoom, zoom_scaled, axis_details)) {
					ret = send_relative_move_ptz(ptz_url_, profile_data_.token_, camera_->username, camera_->password, 0, 0, zoom_scaled);
					if (SOAP_OK == ret) {
						send_response_ = true;
						update_position_ = poll_status(PtzControl::Type::Zoom) ? false : true;
					}
				}
				break;
			}
			case PtzControl::Type::ZoomPlus:
			{
				float zoom_scaled = 0.0; 
				float zoom_degree = 0.4; 
				AxisDetails axis_details;
				if (scale_cam_rel_values(Axis::Zoom, ptz_details_, zoom_degree, zoom_scaled, axis_details)) {
					ret = send_relative_move_ptz(ptz_url_, profile_data_.token_, camera_->username, camera_->password, 0, 0, zoom_scaled);
					if (SOAP_OK == ret) {
						send_response_ = true;
						update_position_ = poll_status(PtzControl::Type::ZoomPlus) ? false : true;
					}
				}				
				break;
			}
			case PtzControl::Type::ZoomMinus:
			{
				float zoom_scaled = 0.0; 
				float zoom_degree = -0.4; 
				AxisDetails axis_details;
				if (scale_cam_rel_values(Axis::Zoom, ptz_details_, zoom_degree, zoom_scaled, axis_details)) {
					ret = send_relative_move_ptz(ptz_url_, profile_data_.token_, camera_->username, camera_->password, 0, 0, zoom_scaled);
					if (SOAP_OK == ret){
						send_response_ = true;
						update_position_ = poll_status(PtzControl::Type::ZoomMinus) ? false : true;
					}
				}
				break;
			}
			case PtzControl::Type::SystemReboot:
			{
				// Caveat - this command will reset the camera
				ret = system_reboot(device_url_, camera_->username, camera_->password);
				send_response_ = false;
				update_position_ = false;
				break;
			}
			case PtzControl::Type::GetPresets:
			{
				ret = get_presets(ptz_url_, profile_data_.token_, camera_->username, camera_->password, presets_);
				send_response_ = false;
				update_position_ = false;				
				break;
			}			
			case PtzControl::Type::SetPreset:
			{
				if (SOAP_OK == get_presets(ptz_url_, profile_data_.token_, camera_->username, camera_->password, presets_)) {
					CameraPreset preset;
					std::string token = std::to_string(data->token);
					if (locate_preset(presets_, token, preset)) {
						// Remove preset first then re-add
						ret = remove_preset(ptz_url_, profile_data_.token_, preset.token_, camera_->username, camera_->password);
						if (SOAP_OK == ret)
							ret = set_preset(ptz_url_, profile_data_.token_, token, token, camera_->username, camera_->password);
					} else {
						ret = set_preset(ptz_url_, profile_data_.token_, token, token, camera_->username, camera_->password);
					}
					if (SOAP_OK == ret)
						ret = poll_status(PtzControl::Type::SetPreset, data->token) ? SOAP_OK : SOAP_ERR;
				}
				send_response_ = true;
				update_position_ = false;				
				break;
			}
			case PtzControl::Type::GotoPreset:
			{
				std::string token = std::to_string(data->token);
				ret = goto_preset(ptz_url_, profile_data_.token_, token, camera_->username, camera_->password);
				if (SOAP_OK == ret) {
					send_response_ = true;
					update_position_ = poll_status(PtzControl::Type::GotoPreset) ? false : true;
				}
				break;
			}
			case PtzControl::Type::SetHomePosition:
			{
				ret = set_home_position(ptz_url_, profile_data_.token_, camera_->username, camera_->password);
				send_response_ = true;
				update_position_ = false;
				break;
			}
			case PtzControl::Type::GotoHomePosition:
			case PtzControl::Type::PanTiltZoomReset:
			{
				ret = goto_home_position(ptz_url_, profile_data_.token_, camera_->username, camera_->password);
				if (SOAP_OK == ret) {
					send_response_ = true;
					update_position_ = poll_status(PtzControl::Type::GotoHomePosition) ? false : true;
				}
				break;
			}
			case PtzControl::Type::SelectiveZoom:
			{
				if (common::Utilities::curl(create_aux_url(data), camera_->auth_type, camera_->username, camera_->password)) {
					send_response_ = true;
					ret = SOAP_OK;
					update_position_ = poll_status(PtzControl::Type::SelectiveZoom) ? false : true;
				} else {
					logger()->error("OnvifControl::{} curl error = {}", __func__, common::Utilities::cb_buffer_ ->get_data());
				}
				break;
			}
			case PtzControl::Type::FocusNear:
			{
				if (profile_data_.cont_focus_) {
					float speed = -3.0;
					ret = img_cont_move_focus(imaging_url_, profile_data_.video_src_token_, camera_->username, camera_->password, speed);
				} else {
					logger()->error("OnvifControl::{} profile token = {} video source token = {} doesn't support continuous focus", __func__, profile_data_.token_, profile_data_.video_src_token_);
				}
				break;
			}				
			case PtzControl::Type::FocusFar:
			{
				if (profile_data_.cont_focus_) {
					float speed = 3.0;
					ret = img_cont_move_focus(imaging_url_, profile_data_.video_src_token_, camera_->username, camera_->password, speed);
				} else {
					logger()->error("OnvifControl::{} profile token = {} video source token = {} doesn't support continuous focus", __func__, profile_data_.token_, profile_data_.video_src_token_);
				}
				break;
			}
			case PtzControl::Type::FocusStop:
			{
				ret = img_move_stop(imaging_url_, profile_data_.video_src_token_, camera_->username, camera_->password);
				break;
			}
			default:
				send_response_ = false;
				logger()->trace("OnvifControl::{} Unsupported PTZ command = {} (exit)", __func__, PtzControl::commands_[(PtzControl::Type) data->type]);
				break;
		}
	}

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return (SOAP_OK == ret);	
}

std::string OnvifControl::create_aux_url(const data_ptr_t& data)
{
	logger()->trace("OnvifControl::{} (entry)", __func__);

	std::string url;
	if (camera_ && !camera_->ptz_control_ip.empty()) {
		url = std::string("http://") + camera_->ptz_control_ip + std::string(":");
		if (camera_->ptz_control_port > 0)
			url += std::to_string(camera_->ptz_control_port);
		else
			url += "80";

		// width/height percentage start/end x/y
		float wpsx = data->spos_x/(1.0 * data->width);
		float wpsy = data->spos_y/(1.0 * data->height);
		float hpsx = data->epos_x/(1.0 * data->width);
		float hpsy = data->epos_y/(1.0 * data->height);

		uint16_t scaled_spos_x = ceil(wpsx * profiles_[0].x_);
		uint16_t scaled_spos_y = ceil(wpsy * profiles_[0].y_);
		uint16_t scaled_epos_x = ceil(hpsx * profiles_[0].x_);
		uint16_t scaled_epos_y = ceil(hpsy * profiles_[0].y_);

		url += std::string("/cgi-bin/camctrl?sposition_x=") + std::to_string(scaled_spos_x) \
			+ std::string("&sposition_y=") + std::to_string(scaled_spos_y) \
			+ std::string("&eposition_x=") + std::to_string(scaled_epos_x) \
			+ std::string("&eposition_y=") + std::to_string(scaled_epos_y) \
			+ std::string("&resolution=") + std::to_string(profiles_[0].x_) \
			+ std::string("&Language=") + std::to_string(data->language);
	}

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, url);
	return url;
}

void OnvifControl::debug_ptz_node()
{
	logger()->trace("OnvifControl::{} home position supported = {} fixed home = {} maximum no. of presets = {}",
		__func__, ptz_details_.home_support_, ptz_details_.fixed_home_pos_, ptz_details_.max_preset_);

	for (uint32_t i = 0; i < ptz_details_.ptz_axis_.size(); i++) {
		logger()->trace("OnvifControl::{} axis = {} min x = {} max x = {} min y = {} max y = {}",
			__func__, ptz_details_.ptz_axis_[i].name_, ptz_details_.ptz_axis_[i].fx_min_,
			ptz_details_.ptz_axis_[i].fx_max_, ptz_details_.ptz_axis_[i].fy_min_, ptz_details_.ptz_axis_[i].fy_max_);
	}
}

std::string OnvifControl::to_str(Status status)
{
	std::string ret;

	switch(status) {
		case Status::Idle:
			ret = "Idle";
			break;
		case  Status::Moving:
			ret = "Moving";
			break;		
		case Status::Unknown:
		default:
			ret = "Unknown";
			break;
	}

	return ret;
}

// Image related 
int OnvifControl::img_get_move_options(const std::string& imaging, ProfileData& data, const std::string& username, const std::string& password)
{
	logger()->trace("OnvifControl::{} imaging = {} token = {}  username = {} password = {} (entry)", __func__, imaging, data.video_src_token_, username, password);
	int ret = SOAP_ERR;

	ImagingBindingProxy proxy(imaging.c_str());

	_timg__GetMoveOptions timg__GetMoveOptions;
	_timg__GetMoveOptionsResponse response;

	timg__GetMoveOptions.VideoSourceToken = data.video_src_token_;

	add_credential(proxy.soap, username, password);

	ret = proxy.GetMoveOptions(&timg__GetMoveOptions, &response);
	if (SOAP_OK == ret) {
		logger()->trace("OnvifControl::{} success", __func__);
		if (response.MoveOptions) {
			data.abs_focus_ = (response.MoveOptions->Absolute) ? true : false;
			data.rel_focus_ = (response.MoveOptions->Relative) ? true : false;
			data.cont_focus_ = (response.MoveOptions->Continuous) ? true : false;
			logger()->trace("OnvifControl::{}  focus, absolute = {} relative = {} continuous = {}", __func__,
				(data.abs_focus_ ? "enabled" : "disabled"), (data.rel_focus_ ? "enabled" : "disabled"), (data.cont_focus_ ? "enabled" : "disabled"));
		}
	} else {
		logger()->error("OnvifControl::{} failed error = {}", __func__, (response.soap && response.soap->fault && response.soap->fault->faultstring) ? response.soap->fault->faultstring : "unknown");
	}

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

int OnvifControl::img_cont_move_focus(const std::string& imaging, const std::string& token, const std::string& username, const std::string& password, float speed)
{
	logger()->trace("OnvifControl::{} imaging = {} token = {}  username = {} password = {} speed = {} (entry)", __func__, imaging, token, username, password, speed);
	int ret = SOAP_ERR;

	ImagingBindingProxy proxy(imaging.c_str());

	_timg__Move timg__Move;
	_timg__MoveResponse response;

	tt__FocusMove focus;
	tt__ContinuousFocus c;
	c.Speed = speed;
	focus.Continuous = &c;

	timg__Move.VideoSourceToken = token;
	timg__Move.Focus = &focus;
	
	add_credential(proxy.soap, username, password);

	ret = proxy.Move(&timg__Move, &response);
	if (SOAP_OK == ret)
		logger()->trace("OnvifControl::{} success continuous focus speed = {}", __func__, speed);
	else
		logger()->error("OnvifControl::{} failed continuous focus speed = {} error = {}", __func__, speed, (response.soap && response.soap->fault && response.soap->fault->faultstring) ? response.soap->fault->faultstring : "unknown");

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

int OnvifControl::img_move_stop(const std::string& imaging, const std::string& token, const std::string& username, const std::string& password)
{
	logger()->trace("OnvifControl::{} ptz = {} token = {}  username = {} password = {} (entry)", __func__, token, username, password);
	int ret = SOAP_ERR;

	ImagingBindingProxy proxy(imaging.c_str());

	_timg__Stop timg__Stop;
	_timg__StopResponse response;

	timg__Stop.VideoSourceToken = token;

	add_credential(proxy.soap, username, password);

	ret = proxy.Stop(&timg__Stop, &response);
	if (SOAP_OK == ret)
		logger()->trace("OnvifControl::{} success stop", __func__);
	else
		logger()->error("OnvifControl::{} failed stop error = {}", __func__, (response.soap && response.soap->fault && response.soap->fault->faultstring) ? response.soap->fault->faultstring : "unknown");

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

int OnvifControl::get_img_setting(const std::string& imaging, const std::string& token, const std::string& username, const std::string& password)
{
	logger()->trace("OnvifControl::{} imaging = {} token = {}  username = {} password = {} (entry)", __func__, imaging, token, username, password);
	int ret = SOAP_ERR;

	ImagingBindingProxy proxy(imaging.c_str());

	_timg__GetImagingSettings timg__GetImagingSettings;
	_timg__GetImagingSettingsResponse response;

	timg__GetImagingSettings.VideoSourceToken = token;

	add_credential(proxy.soap, username, password);

	ret = proxy.GetImagingSettings(&timg__GetImagingSettings, &response);
	if (SOAP_OK == ret)
		logger()->trace("OnvifControl::{} success {}", __func__);
	else
		logger()->error("OnvifControl::{} failed error = {}", __func__, (response.soap && response.soap->fault && response.soap->fault->faultstring) ? response.soap->fault->faultstring : "unknown");

	logger()->trace("OnvifControl::{} ret = {} (exit)", __func__, ret);
	return ret;
}

}}}