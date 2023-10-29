#pragma once

#include <QString>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace nucleus {

// https://www.topografix.com/gpx.asp
// https://en.wikipedia.org/wiki/GPS_Exchange_Format
// TODO: handle waypoint and route
namespace gpx {

    using TrackPoint = glm::dvec3; // TODO: time?
    using TrackSegment = std::vector<TrackPoint>;
    using TrackType = std::vector<TrackSegment>;

    struct Gpx {
        TrackType track;
    };

    std::unique_ptr<Gpx> parse(const QString& path);

} // namespace gpx

struct Track {
    Track(const gpx::Gpx& gpx);
    std::vector<glm::vec3> points;
};

std::vector<glm::vec3> to_world_points(const gpx::Gpx& gpx);

} // namespace nucleus
