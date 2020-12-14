/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   preset.h
 * Author: <tyrusbautista@gmail.com>
 *
 * Created on March 2, 2020, 10:33 AM
 */

#pragma once

namespace orion {
namespace streamer {
namespace processor {
	
class CameraPreset {
public:
	int		id_;
	std::string name_;
	std::string token_;
	float		x_;
	float		y_;
	float		z_;

	CameraPreset()
		: id_(0)
		, name_("")
		, token_("")
		, x_(0)
		, y_(0)
		, z_(0)
	{
	}

	CameraPreset(int id, const char* name, const char* token, float x, float y, float z)
		: id_(id)
		, name_(name ? name : "")
		, token_(token ? token : "")
		, x_(x)
		, y_(y)
		, z_(z)
	{
	}
};

}}}