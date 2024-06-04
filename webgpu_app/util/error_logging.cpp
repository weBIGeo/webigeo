/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Gerald Kimmersdorfer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#include "error_logging.h"

#include <QDateTime>
#include <QFileInfo>
#include <iostream>

void qt_logging_callback(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    // const char *function = context.function ? context.function : "";
    const char* typeStr = nullptr;
    const char* colorCode = nullptr;
    std::ostream* stream = nullptr;

    switch (type) {
    case QtDebugMsg:
        typeStr = "Debug   ";
        colorCode = ASCII_COLOR_CYAN;
        stream = &std::cout;
        break;
    case QtInfoMsg:
        typeStr = "Info    ";
        colorCode = ASCII_COLOR_BLUE;
        stream = &std::cout;
        break;
    case QtWarningMsg:
        typeStr = "Warning ";
        colorCode = ASCII_COLOR_YELLOW;
        stream = &std::cout;
        break;
    case QtCriticalMsg:
        typeStr = "Critical";
        colorCode = ASCII_COLOR_RED;
#ifdef __EMSCRIPTEN__ // on emscripten we use std::cout for critical messages for the colors to work
        stream = &std::cout;
#else
        stream = &std::cerr;
#endif
        break;
    case QtFatalMsg:
        typeStr = "Fatal   ";
        colorCode = ASCII_COLOR_RED;
        stream = &std::cerr;
        break;
    }

    QString fileName = context.file ? QFileInfo(context.file).fileName() : "";

#ifdef __EMSCRIPTEN__ // Full row color for web
    QString logMessage = "%1%2 | %3%4 | %5" ASCII_COLOR_RESET;
#else
    QString logMessage = "%1%2 | %3%4 |" ASCII_COLOR_RESET " %5";
    if (type == QtDebugMsg) // gray message for debug messages
        logMessage = "%1%2 | %3%4 | " ASCII_COLOR_GRAY "%5" ASCII_COLOR_RESET;
#endif
    logMessage = logMessage.arg(colorCode)
                     .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                     .arg(typeStr)
                     .arg(fileName.isEmpty() ? "" : " | " + fileName + ":" + QString::number(context.line), fileName.isEmpty() ? 0 : -28)
                     .arg(localMsg.constData());

    (*stream) << logMessage.toStdString() << std::endl;

    if (type == QtFatalMsg) {
        abort();
    }
}

std::map<WGPUErrorType, QString> wgpu_error_map = { { WGPUErrorType_NoError, "NoError" }, { WGPUErrorType_Validation, "Validation" },
    { WGPUErrorType_OutOfMemory, "OutOfMemory" }, { WGPUErrorType_Internal, "Internal" }, { WGPUErrorType_Unknown, "Unknown" },
    { WGPUErrorType_DeviceLost, "DeviceLost" }, { WGPUErrorType_Force32, "Force32" } };

void webgpu_device_error_callback(WGPUErrorType type, const char* message, [[maybe_unused]] void* userData)
{
    const auto& typeStr = wgpu_error_map[type];

    QString logMessage = ASCII_COLOR_MAGENTA "%1 | WebGPU   | %2 |" ASCII_COLOR_RESET " %3";
    logMessage = logMessage.arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(typeStr, -25).arg(message);

    std::cout << logMessage.toStdString() << std::endl;
}
