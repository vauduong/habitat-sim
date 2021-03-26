// Copyright (c) Facebook, Inc. and its affiliates.
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "esp/bindings/bindings.h"

#include <Magnum/Magnum.h>
#include <Magnum/SceneGraph/SceneGraph.h>

#include <Magnum/PythonBindings.h>
#include <Magnum/SceneGraph/PythonBindings.h>

#include <utility>

#include "esp/sensor/CameraSensor.h"
#include "esp/sensor/VisualSensor.h"
#ifdef ESP_BUILD_WITH_CUDA
#include "esp/sensor/RedwoodNoiseModel.h"
#endif
#include "esp/sensor/Sensor.h"
#include "esp/sensor/SensorFactory.h"
#include "esp/sim/Simulator.h"

namespace py = pybind11;
using py::literals::operator""_a;

namespace {
template <class T>
esp::scene::SceneNode* nodeGetter(T& self) {
  // TODO(mosra) PR#353
  // NOLINTNEXTLINE(clang-diagnostic-undefined-bool-conversion)
  if (!&self.node())
    throw py::value_error{"feature not valid"};
  return &self.node();
};

// Get the torch tensor or numpy array buffer of the Sensor, initialize if it
// does not exist yet
template <class T>
auto buffer(T& self, int gpuDevice) {
  py::handle handle = py::cast(self);
  if (!py::hasattr(handle, "__buffer")) {
    if (self.isVisualSensor()) {
      esp::sensor::VisualSensor& sensor =
          static_cast<esp::sensor::VisualSensor&>(self);
      if (sensor.specification()->gpu2gpuTransfer) {
        auto torch = py::module_::import("torch");
        if (sensor.specification()->sensorType ==
            esp::sensor::SensorType::Semantic) {
          py::setattr(handle, "__buffer",
                      torch.attr("empty")(
                          (py::int_(sensor.specification()->resolution[0]),
                           py::int_(sensor.specification()->resolution[1])),
                          "dtype"_a = torch.attr("int32"),
                          "device"_a = torch.attr("device")(
                              py::str("cuda"), py::int_(gpuDevice))));
        } else if (sensor.specification()->sensorType ==
                   esp::sensor::SensorType::Depth) {
          py::setattr(handle, "__buffer",
                      torch.attr("empty")(
                          (py::int_(sensor.specification()->resolution[0]),
                           py::int_(sensor.specification()->resolution[1])),
                          "dtype"_a = torch.attr("float32"),
                          "device"_a = torch.attr("device")(
                              py::str("cuda"), py::int_(gpuDevice))));
        } else {
          py::setattr(handle, "__buffer",
                      torch.attr("empty")(
                          (py::int_(sensor.specification()->resolution[0]),
                           py::int_(sensor.specification()->resolution[1]),
                           py::int_(sensor.specification()->channels)),
                          "dtype"_a = torch.attr("uint32"),
                          "device"_a = torch.attr("device")(
                              py::str("cuda"), py::int_(gpuDevice))));
        }
      } else {
        if (sensor.specification()->sensorType ==
            esp::sensor::SensorType::Semantic) {
          auto pyBuffer = py::array(py::buffer_info(
              nullptr,          /* Pointer to data (nullptr -> ask NumPy to
                                  allocate!) */
              sizeof(uint32_t), /* Size of one item */
              py::format_descriptor<uint32_t>::value, /* Buffer format
                                                       */
              2,                                      /* How many dimensions? */
              {sensor.specification()->resolution[0],
               sensor.specification()->resolution[1]}, /* Number of elements for
                                                       each dimension */
              {sizeof(uint32_t) * sensor.specification()->resolution[1],
               sizeof(uint32_t)} /* Strides for each dimension */
              ));
          py::setattr(handle, "__buffer", pyBuffer);
        } else if (sensor.specification()->sensorType ==
                   esp::sensor::SensorType::Depth) {
          auto pyBuffer = py::array(py::buffer_info(
              nullptr,       /* Pointer to data (nullptr -> ask NumPy to
                                allocate!) */
              sizeof(float), /* Size of one item */
              py::format_descriptor<float>::value, /* Buffer format */
              2,                                   /* How many dimensions? */
              {sensor.specification()->resolution[0],
               sensor.specification()->resolution[1]}, /* Number of elements for
                                                       each dimension */
              {sizeof(float) * sensor.specification()->resolution[1],
               sizeof(float)} /* Strides for each dimension */
              ));
          py::setattr(handle, "__buffer", pyBuffer);
        } else {
          auto pyBuffer = py::array(py::buffer_info(
              nullptr,         /* Pointer to data (nullptr -> ask NumPy to
                                  allocate!) */
              sizeof(uint8_t), /* Size of one item */
              py::format_descriptor<uint8_t>::value, /* Buffer format */
              3,                                     /* How many dimensions? */
              {sensor.specification()->resolution[0],
               sensor.specification()->resolution[1],
               sensor.specification()->channels}, /* Number of elements
                                                   for each dimension */
              {sizeof(uint8_t) * sensor.specification()->resolution[1] *
                   sensor.specification()->channels,
               sizeof(uint8_t), sizeof(uint8_t)}
              /* Strides for each dimension */
              ));
          py::setattr(handle, "__buffer", pyBuffer);
        }
      }
    }
  }
  return py::getattr(handle, "__buffer");
};

}  // namespace

namespace esp {
namespace sensor {

void initSensorBindings(py::module& m) {
  // ==== Observation ====
  // NOLINTNEXTLINE(bugprone-unused-raii)
  py::class_<Observation, Observation::ptr>(m, "Observation");

  // TODO fill out other SensorTypes
  // ==== enum SensorType ====
  py::enum_<SensorType>(m, "SensorType")
      .value("NONE", SensorType::None)
      .value("COLOR", SensorType::Color)
      .value("DEPTH", SensorType::Depth)
      .value("SEMANTIC", SensorType::Semantic);

  py::enum_<SensorSubType>(m, "SensorSubType")
      .value("NONE", SensorSubType::None)
      .value("PINHOLE", SensorSubType::Pinhole)
      .value("ORTHOGRAPHIC", SensorSubType::Orthographic);

  // ==== SensorSpec ====
  py::class_<SensorSpec, SensorSpec::ptr>(m, "SensorSpec", py::dynamic_attr())
      .def(py::init(&SensorSpec::create<>))
      .def_readwrite("uuid", &SensorSpec::uuid)
      .def_readwrite("sensor_type", &SensorSpec::sensorType)
      .def_readwrite("sensor_subtype", &SensorSpec::sensorSubType)
      .def_readwrite("position", &SensorSpec::position)
      .def_readwrite("orientation", &SensorSpec::orientation)
      .def_readwrite("noise_model", &SensorSpec::noiseModel)
      .def_property(
          "noise_model_kwargs",
          [](SensorSpec& self) -> py::dict {
            py::handle handle = py::cast(self);
            if (!py::hasattr(handle, "__noise_model_kwargs")) {
              py::setattr(handle, "__noise_model_kwargs", py::dict());
            }
            return py::getattr(handle, "__noise_model_kwargs");
          },
          [](SensorSpec& self, py::dict v) {
            py::setattr(py::cast(self), "__noise_model_kwargs", std::move(v));
          })
      .def("is_visual_sensor_spec", &SensorSpec::isVisualSensorSpec)
      .def("__eq__", &SensorSpec::operator==)
      .def("__neq__", &SensorSpec::operator!=);

  // ==== VisualSensorSpec ====
  py::class_<VisualSensorSpec, VisualSensorSpec::ptr, SensorSpec>(
      m, "VisualSensorSpec", py::dynamic_attr())
      .def(py::init(&VisualSensorSpec::create<>))
      .def_readwrite("near", &VisualSensorSpec::near)
      .def_readwrite("far", &VisualSensorSpec::far)
      .def_readwrite("resolution", &VisualSensorSpec::resolution)
      .def_readwrite("gpu2gpu_transfer", &VisualSensorSpec::gpu2gpuTransfer)
      .def_readwrite("channels", &VisualSensorSpec::channels);

  // ====CameraSensorSpec ====
  py::class_<CameraSensorSpec, CameraSensorSpec::ptr, VisualSensorSpec,
             SensorSpec>(m, "CameraSensorSpec", py::dynamic_attr())
      .def(py::init(&CameraSensorSpec::create<>))
      .def_readwrite("ortho_scale", &CameraSensorSpec::orthoScale);

  // ==== SensorFactory ====
  py::class_<SensorFactory>(m, "SensorFactory")
      .def("create_sensors", &SensorFactory::createSensors)
      .def("delete_sensor", &SensorFactory::deleteSensor)
      .def("delete_subtree_sensor", &SensorFactory::deleteSubtreeSensor);

  // ==== SensorSuite ====
  py::class_<SensorSuite, Magnum::SceneGraph::PyFeature<SensorSuite>,
             Magnum::SceneGraph::AbstractFeature3D,
             Magnum::SceneGraph::PyFeatureHolder<SensorSuite>>(m, "SensorSuite")
      .def("add", &SensorSuite::add)
      .def("remove", py::overload_cast<const Sensor&>(&SensorSuite::remove))
      .def("remove",
           py::overload_cast<const std::string&>(&SensorSuite::remove))
      .def("clear", &SensorSuite::clear)
      .def("get", &SensorSuite::get)
      .def("get_sensors",
           py::overload_cast<>(&SensorSuite::getSensors, py::const_));

  // ==== Sensor ====
  py::class_<Sensor, Magnum::SceneGraph::PyFeature<Sensor>,
             Magnum::SceneGraph::AbstractFeature3D,
             Magnum::SceneGraph::PyFeatureHolder<Sensor>>(m, "Sensor",
                                                          py::dynamic_attr())
      .def("specification", &Sensor::specification)
      .def("set_transformation_from_spec", &Sensor::setTransformationFromSpec)
      .def("is_visual_sensor", &Sensor::isVisualSensor)
      .def("get_observation", &Sensor::getObservation)
      .def_property_readonly("node", nodeGetter<Sensor>,
                             "Node this object is attached to")
      .def_property_readonly("object", nodeGetter<Sensor>, "Alias to node")
      .def(
          "buffer", buffer<Sensor>,
          R"(Get the torch tensor or numpy array buffer of the Sensor, initialize if it does not exist yet)");

  // ==== VisualSensor ====
  py::class_<VisualSensor, Magnum::SceneGraph::PyFeature<VisualSensor>, Sensor,
             Magnum::SceneGraph::PyFeatureHolder<VisualSensor>>(m,
                                                                "VisualSensor")
      .def(
          "draw_observation", &VisualSensor::drawObservation,
          R"(Draw an observation to the frame buffer using simulator's renderer)")
      .def_property_readonly(
          "render_camera", &VisualSensor::getRenderCamera,
          R"(Get the RenderCamera in the sensor (if there is one) for rendering PYTHON DOES NOT GET OWNERSHIP)",
          pybind11::return_value_policy::reference)
      .def_property_readonly(
          "near", &VisualSensor::getNear,
          R"(The distance to the near clipping plane this VisualSensor uses.)")
      .def_property_readonly(
          "far", &VisualSensor::getFar,
          R"(The distance to the far clipping plane this VisualSensor uses.)")
      .def_property_readonly("hfov", &VisualSensor::getFOV,
                             R"(The Field of View this VisualSensor uses.)")
      .def_property_readonly("framebuffer_size", &VisualSensor::framebufferSize)
      .def_property_readonly("render_target", &VisualSensor::renderTarget);

  // === CameraSensor ====
  py::class_<CameraSensor, Magnum::SceneGraph::PyFeature<CameraSensor>,
             VisualSensor, Magnum::SceneGraph::PyFeatureHolder<CameraSensor>>(
      m, "CameraSensor")
      .def(py::init_alias<std::reference_wrapper<scene::SceneNode>,
                          const CameraSensorSpec::ptr&>())
      .def("set_projection_params", &CameraSensor::setProjectionParameters,
           R"(Specify the projection parameters this CameraSensor should use.
           Should be consumed by first querying this CameraSensor's SensorSpec
           and then modifying as necessary.)",
           "sensor_spec"_a)
      .def("zoom", &CameraSensor::modifyZoom,
           R"(Modify Orthographic Zoom or Perspective FOV multiplicatively by
          passed amount. User >1 to increase, 0<factor<1 to decrease.)",
           "factor"_a)
      .def("reset_zoom", &CameraSensor::resetZoom,
           R"(Reset Orthographic Zoom or Perspective FOV.)")
      .def(
          "set_width", &CameraSensor::setWidth,
          R"(Set the width of the resolution in SensorSpec for this CameraSensor.)")
      .def(
          "set_height", &CameraSensor::setHeight,
          R"(Set the height of the resolution in the SensorSpec for this CameraSensor.)")
      .def_property(
          "fov",
          static_cast<Mn::Deg (CameraSensor::*)() const>(&CameraSensor::getFOV),
          static_cast<void (CameraSensor::*)(Mn::Deg)>(&CameraSensor::setFOV),
          R"(Set the field of view to use for this CameraSensor.  Only applicable to
          Pinhole Camera Types)")
      .def_property(
          "camera_type", &CameraSensor::getCameraType,
          &CameraSensor::setCameraType,
          R"(The type of projection (ORTHOGRAPHIC or PINHOLE) this CameraSensor uses.)")
      .def_property(
          "near_plane_dist", &CameraSensor::getNear, &CameraSensor::setNear,
          R"(The distance to the near clipping plane for this CameraSensor uses.)")
      .def_property(
          "far_plane_dist", &CameraSensor::getFar, &CameraSensor::setFar,
          R"(The distance to the far clipping plane for this CameraSensor uses.)");
#ifdef ESP_BUILD_WITH_CUDA
  py::class_<RedwoodNoiseModelGPUImpl, RedwoodNoiseModelGPUImpl::uptr>(
      m, "RedwoodNoiseModelGPUImpl")
      .def(py::init(&RedwoodNoiseModelGPUImpl::create_unique<
                    const Eigen::Ref<const Eigen::RowMatrixXf>&, int, float>))
      .def("simulate_from_cpu", &RedwoodNoiseModelGPUImpl::simulateFromCPU)
      .def("simulate_from_gpu", [](RedwoodNoiseModelGPUImpl& self,
                                   std::size_t devDepth, const int rows,
                                   const int cols, std::size_t devNoisyDepth) {
        self.simulateFromGPU(reinterpret_cast<const float*>(devDepth), rows,
                             cols, reinterpret_cast<float*>(devNoisyDepth));
      });
#endif
}

}  // namespace sensor
}  // namespace esp
