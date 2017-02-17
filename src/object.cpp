/////////////////////////////////////////////////////////////////////////////
// Name:        object.cpp
// Author:      Laurent Pugin
// Created:     2005
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "object.h"

//----------------------------------------------------------------------------

#include <assert.h>
#include <iostream>
#include <math.h>
#include <sstream>

//----------------------------------------------------------------------------

#include "boundary.h"
#include "chord.h"
#include "clef.h"
#include "dir.h"
#include "doc.h"
#include "dynam.h"
#include "editorial.h"
#include "functorparams.h"
#include "io.h"
#include "keysig.h"
#include "layer.h"
#include "measure.h"
#include "mensur.h"
#include "metersig.h"
#include "note.h"
#include "page.h"
#include "staff.h"
#include "system.h"
#include "tempo.h"
#include "text.h"
#include "textelement.h"
#include "vrv.h"

namespace vrv {

//----------------------------------------------------------------------------
// Object
//----------------------------------------------------------------------------

unsigned long Object::s_objectCounter = 0;

Object::Object() : BoundingBox()
{
    Init("m-");
    if (s_objectCounter++ == 0) {
        SeedUuid();
    }
}

Object::Object(std::string classid) : BoundingBox()
{
    Init(classid);
    if (s_objectCounter++ == 0) {
        SeedUuid();
    }
}

Object *Object::Clone() const
{
    // This should never happen because the method should be overridden
    assert(false);
    return NULL;
}

Object::Object(const Object &object) : BoundingBox(object)
{
    ClearChildren();
    ResetBoundingBox(); // It does not make sense to keep the values of the BBox
    m_parent = NULL;
    m_classid = object.m_classid;
    m_svgclass = object.m_svgclass;
    m_uuid = object.m_uuid; // for now copy the uuid - to be decided
    m_isModified = true;

    int i;
    for (i = 0; i < (int)object.m_children.size(); i++) {
        Object *current = object.m_children.at(i);
        Object *copy = current->Clone();
        copy->Modify();
        copy->SetParent(this);
        m_children.push_back(copy);
    }
}

Object &Object::operator=(const Object &object)
{
    // not self assignement
    if (this != &object) {
        ClearChildren();
        ResetBoundingBox(); // It does not make sense to keep the values of the BBox
        m_parent = NULL;
        m_classid = object.m_classid;
        m_svgclass = object.m_svgclass;
        m_uuid = object.m_uuid; // for now copy the uuid - to be decided
        m_isModified = true;

        int i;
        for (i = 0; i < (int)object.m_children.size(); i++) {
            Object *current = object.m_children.at(i);
            Object *copy = current->Clone();
            copy->Modify();
            copy->SetParent(this);
            m_children.push_back(copy);
        }
    }
    return *this;
}

Object::~Object()
{
    ClearChildren();
}

void Object::Init(std::string classid)
{
    m_parent = NULL;
    m_isAttribute = false;
    m_isModified = true;
    m_classid = classid;
    this->GenerateUuid();

    Reset();
}

ClassId Object::GetClassId() const
{
    // we should always have the method overridden
    assert(false);
    return OBJECT;
};

void Object::Reset()
{
    ClearChildren();
    ResetBoundingBox();
};

void Object::RegisterInterface(std::vector<AttClassId> *attClasses, InterfaceId interfaceId)
{
    m_attClasses.insert(m_attClasses.end(), attClasses->begin(), attClasses->end());
    m_interfaces.push_back(interfaceId);
}

bool Object::IsBoundaryElement()
{
    if (this->IsEditorialElement() || this->Is(ENDING) || this->Is(SECTION)) {
        BoundaryStartInterface *interface = dynamic_cast<BoundaryStartInterface *>(this);
        assert(interface);
        return (interface->IsBoundary());
    }
    return false;
}

void Object::MoveChildrenFrom(Object *sourceParent, int idx, bool allowTypeChange)
{
    if (this == sourceParent) {
        assert("Object cannot be copied to itself");
    }
    if (!allowTypeChange && (this->GetClassId() != sourceParent->GetClassId())) {
        assert("Object must be of the same type");
    }

    int i;
    for (i = 0; i < (int)sourceParent->m_children.size(); i++) {
        Object *child = sourceParent->Relinquish(i);
        child->SetParent(this);
        if (idx != -1) {
            this->InsertChild(child, idx);
            idx++;
        }
        else {
            this->m_children.push_back(child);
        }
    }
}

void Object::MoveItselfTo(Object *targetParent)
{
    assert(targetParent);
    assert(m_parent);
    assert(m_parent != targetParent);

    Object *relinquishedObject = this->m_parent->Relinquish(this->GetIdx());
    assert(relinquishedObject && (relinquishedObject == this));
    targetParent->AddChild(relinquishedObject);
}

void Object::SetUuid(std::string uuid)
{
    m_uuid = uuid;
};

std::string Object::GetSVGClass(void)
{
    return m_svgclass;
}

void Object::SetSVGClass(const std::string &classcontent)
{
    m_svgclass = classcontent;
}

void Object::AddSVGClass(const std::string &classname)
{
    if (HasSVGClass()) {
        m_svgclass += " ";
    }
    m_svgclass += classname;
}

bool Object::HasSVGClass(void)
{
    return !m_svgclass.empty();
}

void Object::ClearChildren()
{
    ArrayOfObjects::iterator iter;
    for (iter = m_children.begin(); iter != m_children.end(); ++iter) {
        // we need to check if this is the parent
        // ownership might have been given up with Relinquish
        if ((*iter)->m_parent == this) {
            delete *iter;
        }
    }
    m_children.clear();
}

int Object::GetChildCount(const ClassId classId) const
{
    return (int)count_if(m_children.begin(), m_children.end(), ObjectComparison(classId));
}

int Object::GetAttributes(ArrayOfStrAttr *attributes) const
{
    assert(attributes);
    attributes->clear();

    Att::GetCmn(this, attributes);
    Att::GetCmnornaments(this, attributes);
    Att::GetCritapp(this, attributes);
    Att::GetExternalsymbols(this, attributes);
    Att::GetMei(this, attributes);
    Att::GetMensural(this, attributes);
    Att::GetMidi(this, attributes);
    Att::GetPagebased(this, attributes);
    Att::GetShared(this, attributes);

    return (int)attributes->size();
}

bool Object::HasAttribute(std::string attribute, std::string value) const
{
    ArrayOfStrAttr attributes;
    this->GetAttributes(&attributes);
    ArrayOfStrAttr::iterator iter;
    for (iter = attributes.begin(); iter != attributes.end(); iter++) {
        if (((*iter).first == attribute) && ((*iter).second == value)) return true;
    }
    return false;
}

Object *Object::GetFirst(const ClassId classId)
{
    m_iteratorElementType = classId;
    m_iteratorEnd = m_children.end();
    m_iteratorCurrent = std::find_if(m_children.begin(), m_iteratorEnd, ObjectComparison(m_iteratorElementType));
    return (m_iteratorCurrent == m_iteratorEnd) ? NULL : *m_iteratorCurrent;
}

Object *Object::GetNext()
{
    m_iteratorCurrent++;
    m_iteratorCurrent = std::find_if(m_iteratorCurrent, m_iteratorEnd, ObjectComparison(m_iteratorElementType));
    return (m_iteratorCurrent == m_iteratorEnd) ? NULL : *m_iteratorCurrent;
}

Object *Object::GetLast() const
{
    if (m_children.empty()) return NULL;
    return m_children.back();
}

int Object::GetIdx() const
{
    assert(m_parent);

    return m_parent->GetChildIndex(this);
}

void Object::InsertChild(Object *element, int idx)
{
    // With this method we require the parent to be set before
    assert(element->m_parent == this);

    if (idx >= (int)m_children.size()) {
        m_children.push_back(element);
        return;
    }
    ArrayOfObjects::iterator iter = m_children.begin();
    m_children.insert(iter + (idx), element);
}

Object *Object::DetachChild(int idx)
{
    if (idx >= (int)m_children.size()) {
        return NULL;
    }
    Object *child = m_children.at(idx);
    child->m_parent = NULL;
    ArrayOfObjects::iterator iter = m_children.begin();
    m_children.erase(iter + (idx));
    return child;
}

Object *Object::Relinquish(int idx)
{
    if (idx >= (int)m_children.size()) {
        return NULL;
    }
    Object *child = m_children.at(idx);
    child->m_parent = NULL;
    return child;
}

void Object::ClearRelinquishedChildren()
{
    ArrayOfObjects::iterator iter;
    for (iter = m_children.begin(); iter != m_children.end();) {
        if ((*iter)->m_parent != this) {
            iter = m_children.erase(iter);
        }
        else
            iter++;
    }
}

Object *Object::FindChildByUuid(std::string uuid, int deepness, bool direction)
{
    Functor findByUuid(&Object::FindByUuid);
    FindByUuidParams findbyUuidParams;
    findbyUuidParams.m_uuid = uuid;
    this->Process(&findByUuid, &findbyUuidParams, NULL, NULL, deepness, direction);
    return findbyUuidParams.m_element;
}

Object *Object::FindChildByType(ClassId classId, int deepness, bool direction)
{
    AttComparison attComparison(classId);
    return FindChildByAttComparison(&attComparison, deepness, direction);
}

Object *Object::FindChildByAttComparison(AttComparison *attComparison, int deepness, bool direction)
{
    Functor findByAttComparison(&Object::FindByAttComparison);
    FindByAttComparisonParams findByAttComparisonParams(attComparison);
    this->Process(&findByAttComparison, &findByAttComparisonParams, NULL, NULL, deepness, direction);
    return findByAttComparisonParams.m_element;
}

Object *Object::FindChildExtremeByAttComparison(AttComparison *attComparison, int deepness, bool direction)
{
    Functor findExtremeByAttComparison(&Object::FindExtremeByAttComparison);
    FindExtremeByAttComparisonParams findExtremeByAttComparisonParams(attComparison);
    this->Process(&findExtremeByAttComparison, &findExtremeByAttComparisonParams, NULL, NULL, deepness, direction);
    return findExtremeByAttComparisonParams.m_element;
}

void Object::FindAllChildByAttComparison(
    ArrayOfObjects *objects, AttComparison *attComparison, int deepness, bool direction, bool clear)
{
    assert(objects);
    if (clear) objects->clear();

    Functor findAllByAttComparison(&Object::FindAllByAttComparison);
    FindAllByAttComparisonParams findAllByAttComparisonParams(attComparison, objects);
    this->Process(&findAllByAttComparison, &findAllByAttComparisonParams, NULL, NULL, deepness, direction);
}

Object *Object::GetChild(int idx) const
{
    if ((idx < 0) || (idx >= (int)m_children.size())) {
        return NULL;
    }
    return m_children.at(idx);
}

void Object::RemoveChildAt(int idx)
{
    if (idx >= (int)m_children.size()) {
        return;
    }
    delete m_children.at(idx);
    ArrayOfObjects::iterator iter = m_children.begin();
    m_children.erase(iter + (idx));
}

void Object::GenerateUuid()
{
    int nr = std::rand();
    char str[17];
    // I do not want to use a stream for doing this!
    snprintf(str, 16, "%016d", nr);

    m_uuid = m_classid + std::string(str);
    std::transform(m_uuid.begin(), m_uuid.end(), m_uuid.begin(), ::tolower);
}

void Object::ResetUuid()
{
    GenerateUuid();
}

void Object::SeedUuid(unsigned int seed)
{
    // Init random number generator for uuids
    if (seed == 0) {
        std::srand((unsigned int)std::time(0));
    }
    else {
        std::srand(seed);
    }
}

void Object::SetParent(Object *parent)
{
    assert(!m_parent);
    m_parent = parent;
}

void Object::AddChild(Object *child)
{
    // This should never happen because the method should be overridden
    assert(false);
}

int Object::GetDrawingX() const
{
    assert(m_parent);
    return m_parent->GetDrawingX();
}

int Object::GetDrawingY() const
{
    assert(m_parent);
    return m_parent->GetDrawingY();
}

int Object::GetChildIndex(const Object *child)
{
    ArrayOfObjects::iterator iter;
    int i;
    for (iter = m_children.begin(), i = 0; iter != m_children.end(); ++iter, i++) {
        if (child == *iter) {
            return i;
        }
    }
    return -1;
}

void Object::Modify(bool modified)
{
    // if we have a parent and a new modification, propagate it
    if (m_parent && !m_isModified && modified) {
        m_parent->Modify();
    }
    m_isModified = modified;
}

void Object::FillFlatList(ListOfObjects *flatList)
{
    Functor addToFlatList(&Object::AddLayerElementToFlatList);
    AddLayerElementToFlatListParams addLayerElementToFlatListParams(flatList);
    this->Process(&addToFlatList, &addLayerElementToFlatListParams);
}

Object *Object::GetFirstParent(const ClassId classId, int maxDepth) const
{
    if ((maxDepth == 0) || !m_parent) {
        return NULL;
    }

    if (m_parent->GetClassId() == classId) {
        return m_parent;
    }
    else {
        return (m_parent->GetFirstParent(classId, maxDepth - 1));
    }
}

Object *Object::GetLastParentNot(const ClassId classId, int maxDepth)
{
    if ((maxDepth == 0) || !m_parent) {
        return NULL;
    }

    if (m_parent->GetClassId() == classId) {
        return this;
    }
    else {
        return (m_parent->GetLastParentNot(classId, maxDepth - 1));
    }
}

void Object::Process(Functor *functor, FunctorParams *functorParams, Functor *endFunctor,
    ArrayOfAttComparisons *filters, int deepness, bool direction)
{
    if (functor->m_returnCode == FUNCTOR_STOP) {
        return;
    }

    bool processChildren = true;
    if (functor->m_visibleOnly && this->IsEditorialElement()) {
        EditorialElement *editorialElement = dynamic_cast<EditorialElement *>(this);
        assert(editorialElement);
        if (editorialElement->m_visibility == Hidden) {
            processChildren = false;
        }
    }

    functor->Call(this, functorParams);

    // do not go any deeper in this case
    if (functor->m_returnCode == FUNCTOR_SIBLINGS) {
        functor->m_returnCode = FUNCTOR_CONTINUE;
        return;
    }
    else if (this->IsEditorialElement()) {
        // since editorial object doesn't count, we increase the deepness limit
        deepness++;
    }
    if (deepness == 0) {
        // any need to change the functor m_returnCode?
        return;
    }
    deepness--;

    if (processChildren) {
        ArrayOfObjects::iterator iter;
        // We need a pointer to the array for the option to work on a reversed copy
        ArrayOfObjects *children = &this->m_children;
        ArrayOfObjects reversed;
        // For processing backwards, we operated on a copied reversed version
        // Since we hold pointers, only addresses are copied
        if (direction == BACKWARD) {
            reversed = (*children);
            std::reverse(reversed.begin(), reversed.end());
            children = &reversed;
        }
        for (iter = children->begin(); iter != children->end(); ++iter) {
            if (filters && !filters->empty()) {
                bool hasAttComparison = false;
                // first we look if there is a comparison object for the object type (e.g., a Staff)
                ArrayOfAttComparisons::iterator attComparisonIter;
                for (attComparisonIter = filters->begin(); attComparisonIter != filters->end(); attComparisonIter++) {
                    // if yes, we will use it (*attComparisonIter) for evaluating if the object matches
                    // the attribute (see below)
                    Object *o = *iter;
                    if (o->GetClassId() == (*attComparisonIter)->GetType()) {
                        hasAttComparison = true;
                        break;
                    }
                }
                if (hasAttComparison) {
                    // use the operator of the AttComparison object to evaluate the attribute
                    if ((**attComparisonIter)(*iter)) {
                        // the attribute value matches, process the object
                        // LogDebug("%s ", (*iter)->GetClassName().c_str());
                        (*iter)->Process(functor, functorParams, endFunctor, filters, deepness, direction);
                        break;
                    }
                    else {
                        // the attribute value does not match, skip this child
                        continue;
                    }
                }
            }
            // we will end here if there is no filter at all or for the current child type
            (*iter)->Process(functor, functorParams, endFunctor, filters, deepness, direction);
        }
    }

    if (endFunctor) {
        endFunctor->Call(this, functorParams);
    }
}

int Object::Save(FileOutputStream *output)
{
    SaveParams saveParams(output);

    Functor save(&Object::Save);
    // Special case where we want to process all objects
    save.m_visibleOnly = false;
    Functor saveEnd(&Object::SaveEnd);
    this->Process(&save, &saveParams, &saveEnd);

    return true;
}

//----------------------------------------------------------------------------
// ObjectListInterface
//----------------------------------------------------------------------------

ObjectListInterface::ObjectListInterface(const ObjectListInterface &interface)
{
    // actually nothing to do, we just don't want the list to be copied
    m_list.clear();
}

ObjectListInterface &ObjectListInterface::operator=(const ObjectListInterface &interface)
{
    // actually nothing to do, we just don't want the list to be copied
    if (this != &interface) {
        this->m_list.clear();
    }
    return *this;
}

void ObjectListInterface::ResetList(Object *node)
{
    // nothing to do, the list if up to date
    if (!node->IsModified()) {
        return;
    }

    node->Modify(false);
    m_list.clear();
    node->FillFlatList(&m_list);
    this->FilterList(&m_list);
}

ListOfObjects *ObjectListInterface::GetList(Object *node)
{
    ResetList(node);
    return &m_list;
}

int ObjectListInterface::GetListIndex(const Object *listElement)
{
    ListOfObjects::iterator iter;
    int i;
    for (iter = m_list.begin(), i = 0; iter != m_list.end(); ++iter, i++) {
        if (listElement == *iter) {
            return i;
        }
    }
    return -1;
}

Object *ObjectListInterface::GetListFirst(const Object *startFrom, const ClassId classId)
{
    ListOfObjects::iterator it = m_list.begin();
    int idx = GetListIndex(startFrom);
    if (idx == -1) return NULL;
    std::advance(it, idx);
    it = std::find_if(it, m_list.end(), ObjectComparison(classId));
    return (it == m_list.end()) ? NULL : *it;
}

Object *ObjectListInterface::GetListFirstBackward(Object *startFrom, const ClassId classId)
{
    ListOfObjects::iterator it = m_list.begin();
    int idx = GetListIndex(startFrom);
    if (idx == -1) return NULL;
    std::advance(it, idx);
    ListOfObjects::reverse_iterator rit(it);
    rit = std::find_if(rit, m_list.rend(), ObjectComparison(classId));
    return (rit == m_list.rend()) ? NULL : *rit;
}

Object *ObjectListInterface::GetListPrevious(Object *listElement)
{
    ListOfObjects::iterator iter;
    int i;
    for (iter = m_list.begin(), i = 0; iter != m_list.end(); ++iter, i++) {
        if (listElement == *iter) {
            if (i > 0) {
                return *(--iter);
            }
            else {
                return NULL;
            }
        }
    }
    return NULL;
}

Object *ObjectListInterface::GetListNext(Object *listElement)
{
    ListOfObjects::reverse_iterator iter;
    int i;
    for (iter = m_list.rbegin(), i = 0; iter != m_list.rend(); ++iter, i++) {
        if (listElement == *iter) {
            if (i > 0) {
                return *(--iter);
            }
            else {
                return NULL;
            }
        }
    }
    return NULL;
}

//----------------------------------------------------------------------------
// TextListInterface
//----------------------------------------------------------------------------

std::wstring TextListInterface::GetText(Object *node)
{
    // alternatively we could cache the concatString in the interface and instantiate it in FilterList
    std::wstring concatText;
    ListOfObjects *childList = this->GetList(node); // make sure it's initialized
    for (ListOfObjects::iterator it = childList->begin(); it != childList->end(); it++) {
        Text *text = dynamic_cast<Text *>(*it);
        assert(text);
        concatText += text->GetText();
    }
    return concatText;
}

void TextListInterface::FilterList(ListOfObjects *childList)
{
    ListOfObjects::iterator iter = childList->begin();

    while (iter != childList->end()) {
        if (!(*iter)->Is(TEXT)) {
            // remove anything that is not an LayerElement (e.g. Verse, Syl, etc)
            iter = childList->erase(iter);
            continue;
        }
        iter++;
    }
}

//----------------------------------------------------------------------------
// Functor
//----------------------------------------------------------------------------

Functor::Functor()
{
    m_returnCode = FUNCTOR_CONTINUE;
    m_visibleOnly = true;
    obj_fpt = NULL;
}

Functor::Functor(int (Object::*_obj_fpt)(FunctorParams *))
{
    m_returnCode = FUNCTOR_CONTINUE;
    m_visibleOnly = true;
    obj_fpt = _obj_fpt;
}

void Functor::Call(Object *ptr, FunctorParams *functorParams)
{
    // we should have return codes (not just bool) for avoiding to go further down the tree in some cases
    m_returnCode = (*ptr.*obj_fpt)(functorParams);
}

//----------------------------------------------------------------------------
// Object functor methods
//----------------------------------------------------------------------------

int Object::AddLayerElementToFlatList(FunctorParams *functorParams)
{
    AddLayerElementToFlatListParams *params = dynamic_cast<AddLayerElementToFlatListParams *>(functorParams);
    assert(params);

    params->m_flatList->push_back(this);
    // LogDebug("List %d", params->m_flatList->size());

    return FUNCTOR_CONTINUE;
}

int Object::FindByUuid(FunctorParams *functorParams)
{
    FindByUuidParams *params = dynamic_cast<FindByUuidParams *>(functorParams);
    assert(params);

    if (params->m_element) {
        // this should not happen, but just in case
        return FUNCTOR_STOP;
    }

    if (params->m_uuid == this->GetUuid()) {
        params->m_element = this;
        // LogDebug("Found it!");
        return FUNCTOR_STOP;
    }
    // LogDebug("Still looking for uuid...");
    return FUNCTOR_CONTINUE;
}

int Object::FindByAttComparison(FunctorParams *functorParams)
{
    FindByAttComparisonParams *params = dynamic_cast<FindByAttComparisonParams *>(functorParams);
    assert(params);

    if (params->m_element) {
        // this should not happen, but just in case
        return FUNCTOR_STOP;
    }

    // evaluate by applying the AttComparison operator()
    if ((*params->m_attComparison)(this)) {
        params->m_element = this;
        // LogDebug("Found it!");
        return FUNCTOR_STOP;
    }
    // LogDebug("Still looking for the object matching the AttComparison...");
    return FUNCTOR_CONTINUE;
}

int Object::FindExtremeByAttComparison(FunctorParams *functorParams)
{
    FindExtremeByAttComparisonParams *params = dynamic_cast<FindExtremeByAttComparisonParams *>(functorParams);
    assert(params);

    // evaluate by applying the AttComparison operator()
    if ((*params->m_attComparison)(this)) {
        params->m_element = this;
    }
    // continue until the end
    return FUNCTOR_CONTINUE;
}

int Object::FindAllByAttComparison(FunctorParams *functorParams)
{
    FindAllByAttComparisonParams *params = dynamic_cast<FindAllByAttComparisonParams *>(functorParams);
    assert(params);

    // evaluate by applying the AttComparison operator()
    if ((*params->m_attComparison)(this)) {
        params->m_elements->push_back(this);
    }
    // continue until the end
    return FUNCTOR_CONTINUE;
}

int Object::SetCautionaryScoreDef(FunctorParams *functorParams)
{
    SetCautionaryScoreDefParams *params = dynamic_cast<SetCautionaryScoreDefParams *>(functorParams);
    assert(params);

    assert(params->m_currentScoreDef);

    // starting a new staff
    if (this->Is(STAFF)) {
        Staff *staff = dynamic_cast<Staff *>(this);
        assert(staff);
        params->m_currentStaffDef = params->m_currentScoreDef->GetStaffDef(staff->GetN());
        return FUNCTOR_CONTINUE;
    }

    // starting a new layer
    if (this->Is(LAYER)) {
        Layer *layer = dynamic_cast<Layer *>(this);
        assert(layer);
        layer->SetDrawingCautionValues(params->m_currentStaffDef);
        return FUNCTOR_SIBLINGS;
    }

    return FUNCTOR_CONTINUE;
}

int Object::SetCurrentScoreDef(FunctorParams *functorParams)
{
    SetCurrentScoreDefParams *params = dynamic_cast<SetCurrentScoreDefParams *>(functorParams);
    assert(params);

    assert(params->m_upcomingScoreDef);

    // starting a new page
    if (this->Is(PAGE)) {
        Page *page = dynamic_cast<Page *>(this);
        assert(page);
        if (page->m_parent->GetChildIndex(page) == 0) {
            params->m_upcomingScoreDef->SetRedrawFlags(true, true, true, true, false);
            params->m_drawLabels = true;
        }
        page->m_drawingScoreDef = *params->m_upcomingScoreDef;
        return FUNCTOR_CONTINUE;
    }

    // starting a new system
    if (this->Is(SYSTEM)) {
        System *system = dynamic_cast<System *>(this);
        assert(system);
        // This is the only thing we do for now - we need to wait until we reach the first measure
        params->m_currentSystem = system;
        return FUNCTOR_CONTINUE;
    }

    // starting a new measure
    if (this->Is(MEASURE)) {
        Measure *measure = dynamic_cast<Measure *>(this);
        assert(measure);
        bool systemBreak = false;
        bool scoreDefInsert = false;
        // This is the first measure of the system - more to do...
        if (params->m_currentSystem) {
            systemBreak = true;
            // We had a scoreDef so we need to put cautionnary values
            // This will also happend with clef in the last measure - however, the cautionnary functor will not do
            // anything then
            if (params->m_upcomingScoreDef->m_setAsDrawing && params->m_previousMeasure) {
                ScoreDef cautionaryScoreDef = *params->m_upcomingScoreDef;
                SetCautionaryScoreDefParams setCautionaryScoreDefParams(&cautionaryScoreDef);
                Functor setCautionaryScoreDef(&Object::SetCautionaryScoreDef);
                params->m_previousMeasure->Process(&setCautionaryScoreDef, &setCautionaryScoreDefParams);
            }
            // Set the flags we want to have. This also sets m_setAsDrawing to true so the next measure will keep it
            params->m_upcomingScoreDef->SetRedrawFlags(true, true, false, false, false);
            // Set it to the current system (used e.g. for endings)
            params->m_currentSystem->SetDrawingScoreDef(params->m_upcomingScoreDef);
            params->m_currentSystem->GetDrawingScoreDef()->SetDrawLabels(params->m_drawLabels);
            params->m_currentSystem = NULL;
            params->m_drawLabels = false;
        }
        if (params->m_upcomingScoreDef->m_setAsDrawing) {
            scoreDefInsert = true;
            measure->SetDrawingScoreDef(params->m_upcomingScoreDef);
            params->m_currentScoreDef = measure->GetDrawingScoreDef();
            params->m_upcomingScoreDef->SetRedrawFlags(false, false, false, false, true);
            params->m_upcomingScoreDef->m_setAsDrawing = false;
        }
        measure->SetDrawingBarLines(params->m_previousMeasure, systemBreak, scoreDefInsert);
        params->m_previousMeasure = measure;
        return FUNCTOR_CONTINUE;
    }

    // starting a new scoreDef
    if (this->Is(SCOREDEF)) {
        ScoreDef *scoreDef = dynamic_cast<ScoreDef *>(this);
        assert(scoreDef);
        // Replace the current scoreDef with the new one, including its content (staffDef) - this also sets
        // m_setAsDrawing to true so it will then be taken into account at the next measure
        params->m_upcomingScoreDef->ReplaceDrawingValues(scoreDef);
        return FUNCTOR_CONTINUE;
    }

    // starting a new staffDef
    if (this->Is(STAFFDEF)) {
        StaffDef *staffDef = dynamic_cast<StaffDef *>(this);
        assert(staffDef);
        params->m_upcomingScoreDef->ReplaceDrawingValues(staffDef);
    }

    // starting a new staff
    if (this->Is(STAFF)) {
        Staff *staff = dynamic_cast<Staff *>(this);
        assert(staff);
        params->m_currentStaffDef = params->m_currentScoreDef->GetStaffDef(staff->GetN());
        assert(staff->m_drawingStaffDef == NULL);
        staff->m_drawingStaffDef = params->m_currentStaffDef;
        staff->m_drawingLines = params->m_currentStaffDef->GetLines();
        staff->m_drawingNotationType = params->m_currentStaffDef->GetNotationtype();
        if (params->m_currentStaffDef->HasScale()) {
            staff->m_drawingStaffSize = params->m_currentStaffDef->GetScale();
        }
        return FUNCTOR_CONTINUE;
    }

    // starting a new layer
    if (this->Is(LAYER)) {
        Layer *layer = dynamic_cast<Layer *>(this);
        assert(layer);
        // setting the layer stem direction. Alternatively, this could be done in
        // View::DrawLayer. If this (and other things) is kept here, renaming the method to something
        // more generic (PrepareDrawing?) might be a good idea...
        if (layer->m_parent->GetChildCount() > 1) {
            if (layer->m_parent->GetChildIndex(layer) == 0) {
                layer->SetDrawingStemDir(STEMDIRECTION_up);
            }
            else {
                layer->SetDrawingStemDir(STEMDIRECTION_down);
            }
        }
        layer->SetDrawingStaffDefValues(params->m_currentStaffDef);
        return FUNCTOR_CONTINUE;
    }

    // starting a new clef
    if (this->Is(CLEF)) {
        Clef *clef = dynamic_cast<Clef *>(this);
        assert(clef);
        assert(params->m_currentStaffDef);
        StaffDef *upcomingStaffDef = params->m_upcomingScoreDef->GetStaffDef(params->m_currentStaffDef->GetN());
        assert(upcomingStaffDef);
        upcomingStaffDef->SetCurrentClef(clef);
        params->m_upcomingScoreDef->m_setAsDrawing = true;
        return FUNCTOR_CONTINUE;
    }

    // starting a new keysig
    if (this->Is(KEYSIG)) {
        KeySig *keysig = dynamic_cast<KeySig *>(this);
        assert(keysig);
        assert(params->m_currentStaffDef);
        StaffDef *upcomingStaffDef = params->m_upcomingScoreDef->GetStaffDef(params->m_currentStaffDef->GetN());
        assert(upcomingStaffDef);
        upcomingStaffDef->SetCurrentKeySig(keysig);
        params->m_upcomingScoreDef->m_setAsDrawing = true;
        return FUNCTOR_CONTINUE;
    }

    return FUNCTOR_CONTINUE;
}

/*
int Object::AdjustGraceXPos(FunctorParams *functorParams)
{
    AdjustGraceXPosParams *params = dynamic_cast<AdjustGraceXPosParams *>(functorParams);
    assert(params);

    // starting new layer
    if (this->Is(LAYER)) {
        params->m_graceMinPos = 0;
        return FUNCTOR_CONTINUE;
    }

    if (!this->Is(NOTE)) {
        return FUNCTOR_CONTINUE;
    }

    Note *note = dynamic_cast<Note *>(this);
    assert(note);

    if (!note->IsGraceNote() || note->IsChordTone()) {
        params->m_graceMinPos = 0;
        return FUNCTOR_CONTINUE;
    }

    // we should have processed aligned before
    assert(note->GetGraceAlignment());

    // the negative offset is the part of the bounding box that overflows on the left
    // |____x_____|
    //  ---- = negative offset
    int negative_offset = -(note->GetContentX1())
        + (params->m_doc->GetLeftMargin(NOTE) * params->m_doc->GetDrawingUnit(100) / PARAM_DENOMINATOR);

    if (params->m_graceMinPos > 0) {
        //(*minPos) += (doc->GetLeftMargin(&typeid(*note)) * doc->GetDrawingUnit(100) / PARAM_DENOMINATOR);
    }

    // this should never happen (but can with glyphs not exactly registered at position x=0 in the SMuFL font used)
    if (negative_offset < 0) negative_offset = 0;

    // check if the element overlaps with the preceeding one given by (*minPos)
    int overlap = params->m_graceMinPos - note->GetGraceAlignment()->GetXRel() + negative_offset;

    if ((note->GetGraceAlignment()->GetXRel() - negative_offset) < params->m_graceMinPos) {
        note->GetGraceAlignment()->SetXShift(overlap);
    }

    // the next minimal position if given by the right side of the bounding box + the spacing of the element
    params->m_graceMinPos = note->GetGraceAlignment()->GetXRel() + note->GetContentX2()
        + params->m_doc->GetRightMargin(NOTE) * params->m_doc->GetDrawingUnit(100) / PARAM_DENOMINATOR;
    //(*minPos) = note->GetGraceAlignment()->GetXRel() + note->m_contentBB_x2;
    // note->GetGraceAlignment()->SetMaxWidth(note->m_contentBB_x2 + doc->GetRightMargin(&typeid(*note)) *
    // doc->GetDrawingUnit(100) /
    // PARAM_DENOMINATOR);
    note->GetGraceAlignment()->SetMaxWidth(note->GetContentX2());

    return FUNCTOR_CONTINUE;
}
*/

int Object::GetAlignmentLeftRight(FunctorParams *functorParams)
{
    GetAlignmentLeftRightParams *params = dynamic_cast<GetAlignmentLeftRightParams *>(functorParams);
    assert(params);

    if (!this->IsLayerElement()) return FUNCTOR_CONTINUE;

    int refLeft = this->GetSelfLeft();
    if (params->m_minLeft > refLeft) params->m_minLeft = refLeft;

    int refRight = this->GetSelfRight();
    if (params->m_maxRight < refRight) params->m_maxRight = refRight;

    return FUNCTOR_CONTINUE;
}

int Object::SetOverflowBBoxes(FunctorParams *functorParams)
{
    SetOverflowBBoxesParams *params = dynamic_cast<SetOverflowBBoxesParams *>(functorParams);
    assert(params);

    // starting a new staff
    if (this->Is(STAFF)) {
        Staff *currentStaff = dynamic_cast<Staff *>(this);
        assert(currentStaff);
        assert(currentStaff->GetAlignment());

        params->m_staffAlignment = currentStaff->GetAlignment();

        // currentStaff->GetAlignment()->SetMaxHeight(currentStaff->m_contentBB_y1);

        return FUNCTOR_CONTINUE;
    }

    // starting new layer
    if (this->Is(LAYER)) {
        Layer *currentLayer = dynamic_cast<Layer *>(this);
        assert(currentLayer);
        // set scoreDef attr
        if (currentLayer->GetStaffDefClef()) {
            currentLayer->GetStaffDefClef()->SetOverflowBBoxes(params);
        }
        if (currentLayer->GetStaffDefKeySig()) {
            currentLayer->GetStaffDefKeySig()->SetOverflowBBoxes(params);
        }
        if (currentLayer->GetStaffDefMensur()) {
            currentLayer->GetStaffDefMensur()->SetOverflowBBoxes(params);
        }
        if (currentLayer->GetStaffDefMeterSig()) {
            currentLayer->GetStaffDefMeterSig()->SetOverflowBBoxes(params);
        }
        return FUNCTOR_CONTINUE;
    }

    if (this->IsSystemElement()) {
        return FUNCTOR_CONTINUE;
    }

    if (this->IsControlElement()) {
        return FUNCTOR_CONTINUE;
    }

    if (!this->IsLayerElement()) {
        return FUNCTOR_CONTINUE;
    }

    if (this->Is(SYL)) {
        // We don't want to add the syl to the overflow since lyrics require a full line anyway
        return FUNCTOR_CONTINUE;
    }

    LayerElement *current = dynamic_cast<LayerElement *>(this);
    assert(current);

    if (!current->HasUpdatedBB()) {
        // if nothing was drawn, do not take it into account
        // assert(false); // quite drastic but this should never happen. If nothing has to be drawn
        // LogDebug("Un-updated bounding box for '%s' '%s'", current->GetClassName().c_str(),
        // current->GetUuid().c_str());
        // then the BB should be set to empty with  Object::SetEmptyBB()
        return FUNCTOR_CONTINUE;
    }

    int staffSize = params->m_staffAlignment->GetStaffSize();

    int overflowAbove = params->m_staffAlignment->CalcOverflowAbove(current);
    if (overflowAbove > params->m_doc->GetDrawingStaffLineWidth(staffSize) / 2) {
        // LogMessage("%s top overflow: %d", current->GetUuid().c_str(), overflowAbove);
        params->m_staffAlignment->SetOverflowAbove(overflowAbove);
        params->m_staffAlignment->AddBBoxAbove(current);
    }

    int overflowBelow = params->m_staffAlignment->CalcOverflowBelow(current);
    if (overflowBelow > params->m_doc->GetDrawingStaffLineWidth(staffSize) / 2) {
        // LogMessage("%s bottom overflow: %d", current->GetUuid().c_str(), overflowBelow);
        params->m_staffAlignment->SetOverflowBelow(overflowBelow);
        params->m_staffAlignment->AddBBoxBelow(current);
    }

    // do not go further down the tree in this case since the bounding box of the first element is already taken
    // into
    // account?
    return FUNCTOR_CONTINUE;
}

int Object::SetOverflowBBoxesEnd(FunctorParams *functorParams)
{
    SetOverflowBBoxesParams *params = dynamic_cast<SetOverflowBBoxesParams *>(functorParams);
    assert(params);

    // starting new layer
    if (this->Is(LAYER)) {
        Layer *currentLayer = dynamic_cast<Layer *>(this);
        assert(currentLayer);
        // set scoreDef attr
        if (currentLayer->GetCautionStaffDefClef()) {
            currentLayer->GetCautionStaffDefClef()->SetOverflowBBoxes(params);
        }
        if (currentLayer->GetCautionStaffDefKeySig()) {
            currentLayer->GetCautionStaffDefKeySig()->SetOverflowBBoxes(params);
        }
        if (currentLayer->GetCautionStaffDefMensur()) {
            currentLayer->GetCautionStaffDefMensur()->SetOverflowBBoxes(params);
        }
        if (currentLayer->GetCautionStaffDefMeterSig()) {
            currentLayer->GetCautionStaffDefMeterSig()->SetOverflowBBoxes(params);
        }
    }
    return FUNCTOR_CONTINUE;
}

int Object::Save(FunctorParams *functorParams)
{
    SaveParams *params = dynamic_cast<SaveParams *>(functorParams);
    assert(params);

    if (!params->m_output->WriteObject(this)) {
        return FUNCTOR_STOP;
    }
    return FUNCTOR_CONTINUE;
}

int Object::SaveEnd(FunctorParams *functorParams)
{
    SaveParams *params = dynamic_cast<SaveParams *>(functorParams);
    assert(params);

    if (!params->m_output->WriteObjectEnd(this)) {
        return FUNCTOR_STOP;
    }
    return FUNCTOR_CONTINUE;
}

} // namespace vrv
