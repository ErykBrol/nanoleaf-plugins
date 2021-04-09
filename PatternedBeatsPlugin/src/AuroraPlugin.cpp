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
   Plugin Name: Patterned Beats
   Date: Sept. 12, 2017
 */

#include "AuroraPlugin.h"
#include "LayoutProcessingUtils.h"
#include "ColorUtils.h"
#include "DataManager.h"
#include "PluginFeatures.h"
#include "Logger.h"

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

void initPlugin();
void getPluginFrame(Frame_t *frames, int *nFrames, int *sleepTime);
void pluginCleanup();

#ifdef __cplusplus
}
#endif

#define FFT_BINS 32
#define MAX_PANELS 50 // Recommended layout supports 30 panels, just to be safe have it at 50
#define ENERGY_THRESHOLD 50 // Threshold below which sensitive frequency
                            // detection picks up more 'beats'

// Layout and palette variables
static LayoutData *layoutData;
static RGB_t *paletteColors = NULL;
static int nColors = 0;

// Start off with whatever color is first in palette
static int whichCol = 0;

// Array of panel color
static int pattern[MAX_PANELS];

// Beat/Instrumental change detection variables
static int avg = 0;
static int latest_min = 0;

/**
* @description: Populate the pattern array with random values of 0 or 1.
* 0 represents the panel will have a white color, 1 means the panel will
* have whatever the current frequency color is
*/
void generatePattern()
{
    for (int i = 0; i < layoutData->nPanels; i++)
    {
        int r = rand() % 2;
        pattern[i] = r;
    }
}

/**
 * @description: Initialize the plugin. Called once, when the plugin is loaded.
 * This function can be used to enable rhythm or advanced features,
 * e.g., to enable energy feature, simply call enableEnergy()
 * It can also be used to load the LayoutData and the colorPalette from the DataManager.
 * Any allocation, if done here, should be deallocated in the plugin cleanup function
 *
 */
void initPlugin()
{
    // Seed random numbers
    srand (time (NULL));

    // Necessary feature and data inits
    layoutData = getLayoutData();
    enableFft(FFT_BINS);
    enableBeatFeatures();
    getColorPalette(&paletteColors, &nColors);

    generatePattern();
}

/*
* @description: Analzye fft bins and determine current active color based on strongest
* frequency. Detect beats and more sensitive frequency changes and generate new
* layout pattern if detected.
*/
void beatDetection() {
    // figure out what frequency is strongest
    int maxBin = 0;
    int maxBinIndex = 0;
    static int maxBinIndexSum = 0;
    int n = 0;

    int bins = 1;
    uint8_t *fftBins = getFftBins();
    uint16_t energyLevel = getEnergy();

    for(int i = 0; i < FFT_BINS; i++)
    {
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

    // If there is a beat, or sensitive freq change set current active color and generate a pattern
    if (getIsBeat() || (maxBin > latest_min + 2 * avg && energyLevel <= ENERGY_THRESHOLD)) {
      
        maxBinIndex = maxBinIndexSum / n;
        maxBinIndexSum = 0;
        n = 0;
        whichCol = maxBinIndex % nColors;

        generatePattern();
    }
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
void getPluginFrame(Frame_t *frames, int *nFrames, int *sleepTime)
{
    int R;
    int G;
    int B;

    beatDetection();

    // Set current RGB of frame to current active color from palette
    R = paletteColors[whichCol].R;
    G = paletteColors[whichCol].G;
    B = paletteColors[whichCol].B;

    // Loop through and render each frame
    for(int i = 0; i < layoutData->nPanels; i++)
    {
        frames[i].panelId = layoutData->panels[i].panelId;

        // Color of frame assigned depending on whether the frame should
        // have background color or active color
        if (pattern[i] == 1)
        {
            frames[i].r = R;
            frames[i].g = G;
            frames[i].b = B;
        }
        else
        {
            frames[i].r = 255;
            frames[i].g = 255;
            frames[i].b = 255;
        }

        frames[i].transTime = 3;
    }

    *nFrames = layoutData->nPanels;
}

/**
 * @description: called once when the plugin is being closed.
 * Do all deallocation for memory allocated in initplugin here
 */
void pluginCleanup()
{
    //do deallocation here
}