//
// Created by charl on 6/3/2024.
//

#ifndef SCENE_H
#define SCENE_H
#include "../Core/Input.h"


class Scene: public UserInputHandler {
public:
    virtual void onFramebufferResized(int width, int height) {}
    virtual void onResizeSettled(int width, int height) {}
};



#endif //SCENE_H
