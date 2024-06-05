//
// Created by charl on 6/3/2024.
//

#ifndef UICOMPONENT_H
#define UICOMPONENT_H

#include <memory>
#include "../Core/Input.h"

class Mesh;

class UIComponent: public UserInputHandler {
public:
   virtual int getDepth() const;
};

struct UIComponentComp
{
   bool operator()(const std::shared_ptr<UIComponent> &lhs, const std::shared_ptr<UIComponent> &rhs) const
   {
      return lhs->getDepth() < rhs->getDepth();
   }
};


#endif //UICOMPONENT_H
