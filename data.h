#pragma once
#include<string>

namespace orion {
namespace streamer {
namespace processor {

struct Data {

	std::string camera_name;
	std::string stream_id;

	uint8_t type;
	int16_t pan;
	int16_t tilt;
	int16_t zoom;
	int16_t iris;
	int16_t focus;
	int16_t token;
	uint8_t status;

	// Selection rectangle coordinates top left(spos_x,spos_y)
	// bottom right (epos_x, epos_y)
	uint16_t spos_x;
	uint16_t spos_y;
	uint16_t epos_x;
	uint16_t epos_y;

	// Resolution where the selection above where taken
	uint16_t width;
	uint16_t height;

	uint8_t language;
		
	Data()
	{
		set();
	}

	void set(const std::string& camera_name = "", const std::string& stream_id = "",
		uint8_t type = 0, uint16_t pan = 0, uint16_t tilt = 0, uint16_t zoom = 0,
		uint16_t iris = 0, uint16_t focus = 0, uint16_t token = 0, uint8_t status = 0,
		uint16_t spos_x = 0, uint16_t spos_y = 0, uint16_t epos_x = 0, uint16_t epos_y = 0,
		uint16_t width = 0, uint16_t height = 0, uint8_t language = 0)
	{
		this->camera_name = camera_name;
		this->stream_id = stream_id;
		this->type = type;
		this->pan = pan;
		this->tilt = tilt;
		this->zoom = zoom;
		this->iris = iris;
		this->focus = focus;
		this->token = token;
		this->status = status;
		this->spos_x = spos_x;
		this->spos_y = spos_y;
		this->epos_x = epos_x;
		this->epos_y = epos_y;
		this->width = width;
		this->height = height;
		this->language = language;
	}

	void reset()
	{
		this->type = 0;
		this->pan = 0;
		this->tilt = 0;
		this->zoom = 0;
		this->iris = 0;
		this->focus = 0;
		this->token = 0;
		this->status = 0;
		this->spos_x = 0;
		this->spos_y = 0;
		this->epos_x = 0;
		this->epos_y = 0;
		this->width = 0;
		this->height = 0;
		this->language = 0;		
	}
};

typedef std::shared_ptr<Data> data_ptr_t;

}}}