/*
    Copyright 2017 Nanoleaf Ltd.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
    
   Author: Eryk Brol
   Plugin Name: Soft Lightning
   Date: Sept.12, 2017
 */

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "AuroraPlugin.h"
#include "LayoutProcessingUtils.h"
#include "ColorUtils.h"
#include "DataManager.h"
#include "PluginFeatures.h"
#include "Logger.h"
#include <iostream>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

	void initPlugin();
	void getPluginFrame(Frame_t* frames, int* nFrames, int* sleepTime);
	void pluginCleanup();

#ifdef __cplusplus
}
#endif

#define FFT_BINS 32
#define TILE_DISTANCE 86.6
#define MAX_SOURCES 2 
#define ENERGY_THRESHOLD 50 // Threshold below which sensitive frequency
                            // detection picks up more 'beats'


typedef struct {
	float x, y, dirx, diry;
	float speed;
	float radius;
	int R, G, B;
} light_t;

// Array of light sources
static light_t lights[MAX_SOURCES];
static int nLights = 0;

uint16_t energyLevel = 0;

// Declare some necessary variables
static RGB_t* paletteColours = NULL;
static int nColours = 0;

static LayoutData *layoutData;

// Max values of the layout, used for determining directions of lightning
static float maxY = 0;
static float minY = 0;
static float maxX = 0;
static float minX = 0;

// Beat/Instrumental change detection variables
static int avg = 0;
static int latest_min = 0;

/** Compute cartesian distance between two points */
float distance(float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

/**
* @description: sets light colors based on passed in color and palette
* @param color: int between 0 and nColors, determines palette color
*/
void colorize(int colour) {
	lights[0].R = paletteColours[colour].R;
	lights[0].G = paletteColours[colour].G;
	lights[0].B = paletteColours[colour].B;
}

/**
* @description: returns the max Y coordinate after going through each panel
*/
float getMaxYCoord() {
	float maxY = layoutData->layoutGeometricCenter.y;

	for (int i = 0; i < layoutData->nPanels; i++) {
		if (layoutData->panels[i].shape->getCentroid().y > maxY) {
			maxY = layoutData->panels[i].shape->getCentroid().y;
		}
	}
	return maxY;
}

/**
* @description: returns the min Y coordinate after going through each panel
*/
float getMinYCoord() {
	float minY = layoutData->layoutGeometricCenter.y;

	for (int i = 0; i < layoutData->nPanels; i++) {
		if (layoutData->panels[i].shape->getCentroid().y < minY) {
			minY = layoutData->panels[i].shape->getCentroid().y;
		}
	}
	return minY;
}

/**
* @description: returns the max X coordinate after going through each panel
*/
float getMaxXCoord() {
	float maxX = layoutData->layoutGeometricCenter.x;

	for (int i = 0; i < layoutData->nPanels; i++) {
		if (layoutData->panels[i].shape->getCentroid().x > maxX) {
			maxX = layoutData->panels[i].shape->getCentroid().x;
		}
	}
	return maxX;
}

/**
* @description: returns the min X coordinate after going through each panel
*/
float getMinXCoord() {
	float minX = layoutData->layoutGeometricCenter.x;

	for (int i = 0; i < layoutData->nPanels; i++) {
		if (layoutData->panels[i].shape->getCentroid().x < minX) {
			minX = layoutData->panels[i].shape->getCentroid().x;
		}
	}
	return minX;
}

/**
* @description: removes the 'oldest' light source
* @param idx: determines index of oldest light
*/
void removeLight(int idx) {
	memmove(lights + idx, lights + idx + 1, sizeof(light_t) * (nLights - idx - 1));
  	nLights--;
}

/**
* @description: creates new light source with random X position and either maxY
* or minY
*/
void createLight() {
    // If too many lights already, remove oldest one (not 0 b/c that's background light source)
	if(nLights >= MAX_SOURCES) {
        removeLight(1);
    }

	float topOrBot = (float) rand() / (RAND_MAX);

    // Determine if the new light will come from bottom or top of layout
	if (topOrBot >= 0.5) {
		lights[nLights].y = getMinYCoord();
		lights[nLights].diry = 1;
	} else {
		lights[nLights].y = getMaxYCoord();
		lights[nLights].diry = -1;
	}

    // Lights are white by default
	lights[nLights].R = 255;
	lights[nLights].G = 255;
	lights[nLights].B = 255;

	int dist = (int)maxX - (int)minX;
	
	lights[nLights].x = rand() % dist + (int)minX;
	lights[nLights].dirx = 0;
	lights[nLights].speed = 2 * TILE_DISTANCE;
	lights[nLights].radius = 1;
	nLights++;	
}

/**
 * @description: Initialize the plugin. Called once, when the plugin is loaded.
 * This function can be used to enable rhythm or advanced features,
 * e.g., to enable energy feature, simply call enableEnergy()
 * It can also be used to load the LayoutData and the colorPalette from the DataManager.
 * Any allocation, if done here, should be deallocated in the plugin cleanup function
 *
 */
void initPlugin(){
	srand (time (NULL));
	layoutData = getLayoutData();

	enableFft(FFT_BINS);
	enableBeatFeatures();

	getColorPalette(&paletteColours, &nColours);

	maxY = getMaxYCoord();
	minY = getMinYCoord();
	maxX = getMaxXCoord();
	minX = getMinXCoord();

    // Create main background light (never gets removed)
	lights[0].x = layoutData->layoutGeometricCenter.x;
	lights[0].y = layoutData->layoutGeometricCenter.y;
	nLights++;
}

/*
* @description: Moves all light sources (except main background one) based
* on their direction and speed variables
*/
void propogateSources() {
	for(int i = 1; i < nLights; i++) {
		lights[i].x += lights[i].dirx * lights[i].speed;
		lights[i].y += lights[i].diry * lights[i].speed;

		float d = distance(layoutData->layoutGeometricCenter.x, layoutData->layoutGeometricCenter.y, lights[i].x, lights[i].y);
        if(d > 10.0 * TILE_DISTANCE) {
            removeLight(i);
            i--;
        }
  	}
}

/*
* @description: Nanoleaf funciton that determines color of lights based on distance from center of panels
* adding a diffuse effect. 
* @param *panel: current panel to calculate distance to
* @param *returnR: R color value to return
* @param *returnG: G color value to return
* @param *returnB: B color value to return
*/
void renderPanel(Panel *panel, int *returnR, int *returnG, int *returnB) {
    float R = 0;
    float G = 0;
    float B = 0;
    int i;

	R = lights[0].R;
	G = lights[0].G;
	B = lights[0].B;

	for(i = 1; i < nLights; i++) {
        float d = distance(panel->shape->getCentroid().x, panel->shape->getCentroid().y, lights[i].x, lights[i].y);
        d = d / TILE_DISTANCE;
        d = d - lights[i].radius;
        float d2 = d * d;
        float factor = 1.0 / (d2 * 1.5 + 1.0); 

        R = R * (1.0 - factor) + lights[i].R * factor;
        G = G * (1.0 - factor) + lights[i].G * factor;
        B = B * (1.0 - factor) + lights[i].B * factor;
    }

    *returnR = (int)R;
    *returnG = (int)G;
    *returnB = (int)B;
}

/**
 * @description: this the 'main' function that gives a frame to the Aurora to display onto the panels
 * To obtain updated values of enabled features, simply call get<feature_name>, e.g.,
 * getEnergy(), getIsBeat().
 *
 * If the plugin is a sound visualization plugin, the sleepTime variable will be NULL and is not required to be
 * filled in
 * This function, if is an effects plugin, can specify the interval it is to be called at through the sleepTime variable
 * if its a sound visualization plugin, this function is called at an interval of 50ms or more.
 *
 * @param frames: a pre-allocated buffer of the Frame_t structure to fill up with RGB values to show on panels.
 * Maximum size of this buffer is equal to the number of panels
 * @param nFrames: fill with the number of frames in frames
 * @param sleepTime: specify interval after which this function is called again, NULL if sound visualization plugin
 */
void getPluginFrame(Frame_t* frames, int* nFrames, int* sleepTime){
	int R;
	int G;
	int B;

	// figure out what frequency is strongest
	int maxBin = 0;
	int maxBinIndex = 0;
	static int maxBinIndexSum = 0;
	int n = 0;

    int bins = 1;
	uint8_t * fftBins = getFftBins();
    uint16_t energyLevel = getEnergy();
	for(int i = 0; i < FFT_BINS; i++) {
			if(fftBins[i] > maxBin)
        {
            if (latest_min == 0 || fftBins[i] < latest_min)
            {
                latest_min = fftBins[i];
            } else if (latest_min > 0) {
              latest_min--;
            }

            // Keep a running average of frequency strength to determine threshold
            // for sensitive freq changes
            avg += fftBins[i];
            bins++;
            maxBin = fftBins[i];
            maxBinIndex = i;
        }
	}

    avg /= bins;

	maxBinIndexSum += maxBinIndex;
    n++;

    // Create new light on and onset
	if (getIsOnset() || (maxBin > latest_min + 1.8 * avg && energyLevel <= ENERGY_THRESHOLD)) {
		createLight();
	}

    // Nanoleaf code used to skip call to getBeat(), makes smooth color changes
	#define SKIP_COUNT 1
    static int cnt = 0;
    if (cnt < SKIP_COUNT){
        cnt++;
        return;
    }
    cnt = 0;

    // Change color on beat, depending on strongest freq
	if(getIsBeat() || (maxBin > latest_min + 1.8 * avg && energyLevel <= ENERGY_THRESHOLD)) {
			maxBinIndex = maxBinIndexSum / n;
	        maxBinIndexSum = 0;
	        n = 0;
	        int colour = maxBinIndex % nColours;

			colorize(colour); // which color of the palette this frequency falls under
	}
	

	// iterate through all the panels and render each one
	for(int i = 0; i < layoutData->nPanels; i++) {
			renderPanel(&layoutData->panels[i], &R, &G, &B);
			frames[i].panelId = layoutData->panels[i].panelId;
			frames[i].r = R;
			frames[i].g = G;
			frames[i].b = B;
			frames[i].transTime = 3;
	}

	// move all the light sources
	propogateSources();

	// this algorithm renders every panel at every frame
	*nFrames = layoutData->nPanels;
}

/**
 * @description: called once when the plugin is being closed.
 * Do all deallocation for memory allocated in initplugin here
 */
void pluginCleanup(){
	//do deallocation here
}
