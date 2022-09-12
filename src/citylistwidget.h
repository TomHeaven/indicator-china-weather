/*
 * Copyright (C) 2013 ~ 2019 National University of Defense Technology(NUDT) & Tianjin Kylin Ltd.
 *
 * Authors:
 *  Kobe Lee    lixiang@kylinos.cn/kobe24_lixiang@126.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CITYLISTWIDGET_H
#define CITYLISTWIDGET_H

#include "cityitemwidget.h"

class CityListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CityListWidget(QWidget *parent = 0);
    ~CityListWidget();

    CityItemWidget* getItem(int index);

    void appendItem(CityItemWidget *item);
    void removeItem(CityItemWidget *item);

    int itemCount() const;
    void clearUI();

public slots:
    void updateCityListHeight();

private:
    bool eventFilter(QObject *obj, QEvent *event) Q_DECL_OVERRIDE;

private:
    QVBoxLayout *m_layout = nullptr;
    QTimer *m_timer = nullptr;
};

#endif // CITYLISTWIDGET_H
