/*
 * Bael'Zharon's Respite
 * Copyright (C) 2014 Daniel Skorupski
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "graphics/LandRenderer.h"
#include "graphics/LandRenderData.h"
#include "graphics/Renderer.h"
#include "graphics/util.h"
#include "Camera.h"
#include "Core.h"
#include "Land.h"
#include "LandcellManager.h"
#include "ResourceCache.h"
#include "Texture.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

#include "graphics/shaders/LandVertexShader.h"
#include "graphics/shaders/LandFragmentShader.h"

static const uint32_t kLandscapeTextures[] =
{
    0x06006d6f, // 0x00 BarrenRock
    0x06006d49, // 0x01 Grassland
    0x00000000, // 0x02 Ice
    0x06006d06, // 0x03 LushGrass
    0x00000000, // 0x04 MarshSparseSwamp
    0x00000000, // 0x05 MudRichDirt
    0x00000000, // 0x06 ObsidianPlain
    0x06006d46, // 0x07 PackedDirt
    0x00000000, // 0x08 PatchyDirtFx
    0x06006d3c, // 0x09 PatchyGrassland
    0x00000000, // 0x0A sand-yellow
    0x06006d44, // 0x0B sand-grey
    0x00000000, // 0x0C sand-rockStrewn
    0x00000000, // 0x0D SedimentaryRock
    0x06006d41, // 0x0E SemiBarrenRock
    0x00000000, // 0x0F Snow
    0x06006d45, // 0x10 WaterRunning
    0x00000000, // 0x11 WaterStandingFresh
    0x06006d4f, // 0x12 WaterShallowSea
    0x00000000, // 0x13 WaterShallowStillSea
    0x06006d4e, // 0x14 WaterDeepSea
    0x06006d40, // 0x15 forestfloor
    0x00000000, // 0x16 FauxWaterRunning
    0x00000000, // 0x17 SeaSlime
    0x00000000, // 0x18 Agiland
    0x00000000, // 0x19 Volcano1
    0x00000000, // 0x1A Volcano2
    0x00000000, // 0x1B BlueIce
    0x00000000, // 0x1C Moss
    0x00000000, // 0x1D DarkMoss
    0x00000000, // 0x1E olthoi
    0x00000000, // 0x1F
    // road textures below this line
    0x06006d3f  // 0x20
};

static const int kTerrainArraySize = 512;
static const int kTerrainArrayDepth = sizeof(kLandscapeTextures) / sizeof(kLandscapeTextures[0]);

static const uint32_t kBlendTextures[] =
{
    0xFFFFFFFF, // 0 special case, all white
    0x00000000, // 1 special case, all black
    0x06006d61, // 2 vertical, black to white, left of center
    0x06006d6c, // 3 top left corner, black, semi ragged
    0x06006d6d, // 4 top left corner, black, ragged
    0x06006d60, // 5 top left corner, black, rounded
    0x06006d30, // 6 vertical, black to white, very left of center, wavy
    0x06006d37, // 7 small corner
    0x06006d6b, // 8 big corner
    0x06006d60, // 9 big corner
    0x06006d36  // A wavy diagonal
};

static const int kBlendArraySize = 512;
static const int kBlendArrayDepth = sizeof(kBlendTextures) / sizeof(kBlendTextures[0]);

LandRenderer::LandRenderer()
{
    initProgram();
    initTerrainTexture();
    initBlendTexture();
}

LandRenderer::~LandRenderer()
{
    program_.destroy();
    glDeleteTextures(1, &terrainTexture_);
    glDeleteTextures(1, &blendTexture_);
}

void LandRenderer::render(const glm::mat4& projectionMat, const glm::mat4& viewMat)
{
    program_.use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, terrainTexture_);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, blendTexture_);

    LandcellManager& landcellManager = Core::get().landcellManager();

    glm::vec3 cameraPosition = Core::get().camera().position();
    glUniform4f(program_.getUniform("cameraPosition"), GLfloat(cameraPosition.x), GLfloat(cameraPosition.y), GLfloat(cameraPosition.z), 1.0f);

    glm::vec4 viewLightPosition = viewMat * glm::vec4(lightPosition_.x, lightPosition_.y, lightPosition_.z, 1.0);
    glUniform3f(program_.getUniform("lightPosition"), GLfloat(viewLightPosition.x), GLfloat(viewLightPosition.y), GLfloat(viewLightPosition.z));

    for(auto& pair : landcellManager)
    {
        if(pair.first.isStructure())
        {
            continue;
        }

        int dx = pair.first.x() - landcellManager.center().x();
        int dy = pair.first.y() - landcellManager.center().y();

        glm::vec3 blockPosition(dx * 192.0, dy * 192.0, 0.0);

        const Land& land = static_cast<const Land&>(*pair.second);

        renderLand(land, projectionMat, viewMat, blockPosition);
    }
}

void LandRenderer::setLightPosition(const glm::vec3& lightPosition)
{
    lightPosition_ = lightPosition;
}

void LandRenderer::renderLand(
    const Land& land,
    const glm::mat4& projectionMat,
    const glm::mat4& viewMat,
    const glm::vec3& position)
{
    glm::mat4 worldMat = glm::translate(glm::mat4(), position);

    loadMat3ToUniform(glm::inverseTranspose(glm::mat3(viewMat * worldMat)), program_.getUniform("normalMatrix"));
    loadMat4ToUniform(worldMat, program_.getUniform("worldMatrix"));
    loadMat4ToUniform(viewMat, program_.getUniform("viewMatrix"));
    loadMat4ToUniform(projectionMat, program_.getUniform("projectionMatrix"));

    if(!land.renderData())
    {
        land.renderData().reset(new LandRenderData(land));
    }

    LandRenderData& landRenderData = static_cast<LandRenderData&>(*land.renderData());

    landRenderData.render();
}

void LandRenderer::initProgram()
{
    program_.create();
    program_.attach(GL_VERTEX_SHADER, LandVertexShader);
    program_.attach(GL_FRAGMENT_SHADER, LandFragmentShader);
    program_.link();

    program_.use();

    // samplers
    GLuint terrainTexLocation = program_.getUniform("terrainTex");
    glUniform1i(terrainTexLocation, 0); // corresponds to GL_TEXTURE0

    GLuint blendTexLocation = program_.getUniform("blendTex");
    glUniform1i(blendTexLocation, 1);

    GLuint normalTexLocation = program_.getUniform("normalTex");
    glUniform1i(normalTexLocation, 2);

    // lighting parameters
    glUniform3f(program_.getUniform("lightIntensity"), 1.0f, 1.0f, 1.0f);
    glUniform3f(program_.getUniform("Kd"), 0.7f, 0.7f, 0.7f);
    glUniform3f(program_.getUniform("Ka"), 0.5f, 0.5f, 0.5f);
    glUniform3f(program_.getUniform("Ks"), 0.0f, 0.0f, 0.0f);
    glUniform1f(program_.getUniform("shininess"), 1.0);
}

void LandRenderer::initTerrainTexture()
{
    // allocate terrain texture
    glGenTextures(1, &terrainTexture_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, terrainTexture_);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, Core::get().renderer().textureMinFilter());
    glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, Core::get().renderer().textureMaxAnisotropy());
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB8, kTerrainArraySize, kTerrainArraySize, kTerrainArrayDepth, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

    // populate terrain texture
    for(int i = 0; i < kTerrainArrayDepth; i++)
    {
        Image image;

        if(kLandscapeTextures[i] == 0x00000000)
        {
            image.init(ImageFormat::kRGB24, kTerrainArraySize, kTerrainArraySize, nullptr);
            image.fill(0xFF);
        }
        else
        {
            ResourcePtr texture = Core::get().resourceCache().get(kLandscapeTextures[i]);
            image = texture->cast<Texture>().image;
            image.scale(kTerrainArraySize, kTerrainArraySize);
        }

        GLenum format;

        if(image.format() == ImageFormat::kRGB24)
        {
            format = GL_RGB;
        }
        else if(image.format() == ImageFormat::kBGR24)
        {
            format = GL_BGR;
        }
        else if(image.format() == ImageFormat::kBGRA32)
        {
            format = GL_BGRA;
        }
        else
        {
            throw runtime_error("Bad terrain image format");
        }

        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, kTerrainArraySize, kTerrainArraySize, 1, format, GL_UNSIGNED_BYTE, image.data());
    }

    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
}

void LandRenderer::initBlendTexture()
{
    // allocate terrain texture
    glGenTextures(1, &blendTexture_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, blendTexture_);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, kBlendArraySize, kBlendArraySize, kBlendArrayDepth, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);

    // populate terrain texture
    for(int i = 0; i < kBlendArrayDepth; i++)
    {
        Image image;

        if(kBlendTextures[i] == 0x00000000)
        {
            image.init(ImageFormat::kA8, kBlendArraySize, kBlendArraySize, nullptr);
        }
        else if(kBlendTextures[i] == 0xFFFFFFFF)
        {
            image.init(ImageFormat::kA8, kBlendArraySize, kBlendArraySize, nullptr);
            image.fill(0xFF);
        }
        else
        {
            ResourcePtr texture = Core::get().resourceCache().get(kBlendTextures[i]);
            image = texture->cast<Texture>().image;
        }

        if(image.width() != kBlendArraySize || image.height() != kBlendArraySize)
        {
            throw runtime_error("Bad terrain image size");
        }

        if(image.format() != ImageFormat::kA8)
        {
            throw runtime_error("Bad terrain image format");
        }

        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, kBlendArraySize, kBlendArraySize, 1, GL_RED, GL_UNSIGNED_BYTE, image.data());
    }
}
