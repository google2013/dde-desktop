/**
 * Copyright (C) 2016 Deepin Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 **/

#ifndef CANVASVIEWHELPER_H
#define CANVASVIEWHELPER_H

#include <dfileviewhelper.h>

class CanvasGridView;
class CanvasViewHelper : public DFileViewHelper
{
    Q_OBJECT
public:
    CanvasViewHelper(CanvasGridView *parent);

    CanvasGridView *parent() const;

    virtual const DAbstractFileInfoPointer fileInfo(const QModelIndex &index) const;
    virtual DStyledItemDelegate *itemDelegate() const;
    virtual DFileSystemModel *model() const;
    virtual const DUrlList selectedUrls() const;

};

#endif // CANVASVIEWHELPER_H
