//
// Created by charl on 6/2/2024.
//

#ifndef RENDERER_H
#define RENDERER_H
#include <map>
#include <glad/glad.h>

#include "../Core/Input.h"
#include "../UI/UIComponent.h"
#include "Mesh.h"

class Renderer: public UserInputHandler {
public:
    explicit Renderer(const GLADloadproc &gladLoader);

    void clear();
    void draw();

    void updateMeshes(const std::shared_ptr<UIComponent> &component, const std::vector<Mesh>& meshes);

    void onWindowSizeChanged(int width, int height) override;
private:
    void draw(const std::vector<Mesh> &meshes);

    std::map<std::shared_ptr<UIComponent>, std::vector<Mesh>, UIComponentComp> components;
};



#endif //RENDERER_H
