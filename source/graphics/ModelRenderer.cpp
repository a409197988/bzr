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
#include "graphics/ModelRenderer.h"
#include "graphics/MeshRenderData.h"
#include "graphics/Renderer.h"
#include "graphics/util.h"
#include "Camera.h"
#include "Core.h"
#include "Land.h"
#include "LandcellManager.h"
#include "Model.h"
#include "ModelGroup.h"
#include "ObjectManager.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

#include "graphics/shaders/ModelVertexShader.h"
#include "graphics/shaders/ModelFragmentShader.h"

struct CompareByDepth
{
    CompareByDepth()
    {
        cameraPos_ = Core::get().camera().position();
    }

    bool operator()(const ModelRenderer::DepthSortedModel& a, const ModelRenderer::DepthSortedModel& b) const
    {
        // descending order
        return (cameraPos_ - a.worldPos).length() > (cameraPos_ - b.worldPos).length();
    }

    glm::vec3 cameraPos_;
};

ModelRenderer::ModelRenderer()
{
    program_.create();
    program_.attach(GL_VERTEX_SHADER, ModelVertexShader);
    program_.attach(GL_FRAGMENT_SHADER, ModelFragmentShader);
    program_.link();

    program_.use();

    GLuint texLocation = program_.getUniform("tex");
    glUniform1i(texLocation, 0);
}

ModelRenderer::~ModelRenderer()
{
    program_.destroy();
}

void ModelRenderer::render(const glm::mat4& projectionMat, const glm::mat4& viewMat)
{
    program_.use();

    LandcellManager& landcellManager = Core::get().landcellManager();
    ObjectManager& objectManager = Core::get().objectManager();

    glm::vec3 cameraPosition = Core::get().camera().position();
    glUniform4f(program_.getUniform("cameraPosition"), GLfloat(cameraPosition.x), GLfloat(cameraPosition.y), GLfloat(cameraPosition.z), 1.0f);

    // first pass, render solid objects and collect objects that need depth sorting
    depthSortList_.clear();

    for(auto& pair : objectManager)
    {
        Object& object = *pair.second;

        if(!object.model())
        {
            continue;
        }

        int dx = object.location().landcell.x() - landcellManager.center().x();
        int dy = object.location().landcell.y() - landcellManager.center().y();

        glm::vec3 blockPosition(dx * Land::kBlockSize, dy * Land::kBlockSize, 0.0);

        glm::mat4 worldMat = glm::translate(glm::mat4(), blockPosition + object.location().offset) * glm::mat4_cast(object.location().rotation);

        renderOne(object.model(), projectionMat, viewMat, worldMat);
    }

    for(auto& pair : landcellManager)
    {
        int dx = pair.first.x() - landcellManager.center().x();
        int dy = pair.first.y() - landcellManager.center().y();

        glm::vec3 blockPosition(dx * Land::kBlockSize, dy * Land::kBlockSize, 0.0);

        for(const Doodad& doodad : pair.second->doodads())
        {
            glm::mat4 worldMat = glm::translate(glm::mat4(), blockPosition + doodad.position) * glm::mat4_cast(doodad.rotation);

            renderOne(doodad.resource, projectionMat, viewMat, worldMat);
        }
    }

    // second pass, sort and render objects that need depth sorting
    sort(depthSortList_.begin(), depthSortList_.end(), CompareByDepth());

    for(const DepthSortedModel& depthSortedModel : depthSortList_)
    {
        renderModel(*depthSortedModel.model,
            projectionMat,
            viewMat,
            depthSortedModel.worldMat,
            /*firstPass*/ false);
    }
}

void ModelRenderer::renderOne(const ResourcePtr& resource,
    const glm::mat4& projectionMat,
    const glm::mat4& viewMat,
    const glm::mat4& worldMat)
{
    if(resource->resourceType() == ResourceType::kModelGroup)
    {
        renderModelGroup(resource->cast<ModelGroup>(),
            projectionMat,
            viewMat,
            worldMat);
    }
    else if(resource->resourceType() == ResourceType::kModel)
    {
        renderModel(resource->cast<Model>(),
            projectionMat,
            viewMat,
            worldMat,
            /*firstPass*/ true);
    }
}

void ModelRenderer::renderModelGroup(const ModelGroup& modelGroup,
    const glm::mat4& projectionMat,
    const glm::mat4& viewMat,
    const glm::mat4& worldMat)
{
    for(const ModelGroup::ModelInfo& modelInfo : modelGroup.modelInfos)
    {
        glm::mat4 subWorldMat = glm::translate(glm::mat4(), modelInfo.position) * glm::mat4_cast(modelInfo.rotation) * glm::scale(glm::mat4(), modelInfo.scale);

        renderOne(modelInfo.resource,
            projectionMat,
            viewMat,
            worldMat * subWorldMat);
    }
}

void ModelRenderer::renderModel(const Model& model,
    const glm::mat4& projectionMat,
    const glm::mat4& viewMat,
    const glm::mat4& worldMat,
    bool firstPass)
{
    if(firstPass && model.needsDepthSort)
    {
        glm::vec4 worldPos = worldMat * glm::vec4(0.0, 0.0, 0.0, 1.0);
        DepthSortedModel depthSortedModel = { &model, worldMat, glm::vec3(worldPos.x, worldPos.y, worldPos.z) };
        depthSortList_.push_back(depthSortedModel);
        return;
    }

    loadMat4ToUniform(worldMat, program_.getUniform("worldMatrix"));
    loadMat4ToUniform(viewMat, program_.getUniform("viewMatrix"));
    loadMat4ToUniform(projectionMat, program_.getUniform("projectionMatrix"));

    if(!model.renderData)
    {
        model.renderData.reset(new MeshRenderData(model));
    }

    MeshRenderData& renderData = static_cast<MeshRenderData&>(*model.renderData);

    renderData.render();
}
