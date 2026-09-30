# Original physics source lists mirror the existing Waf manifests.
add_library(hk_base STATIC EXCLUDE_FROM_ALL
    ../../ivp/havana/havok/hk_base/memory/memory.cpp
    ../../ivp/havana/havok/hk_base/memory/memory_util.cpp
    ../../ivp/havana/havok/hk_base/array/array.cpp
    ../../ivp/havana/havok/hk_base/hash/hash.cpp
    ../../ivp/havana/havok/hk_base/stopwatch/stopwatch.cpp
    ../../ivp/havana/havok/hk_base/string/string.cpp
    ../../ivp/havana/havok/hk_base/id_server/id_server.cpp
    ../../ivp/havana/havok/hk_base/base_types.cpp
    ../../ivp/havana/havok/hk_base/console.cpp
)
target_compile_features(hk_base PRIVATE cxx_std_11)
target_compile_definitions(hk_base PRIVATE
    $<TARGET_PROPERTY:ToGLESRuntime,COMPILE_DEFINITIONS>
    VPHYSICS_EXPORTS=1 HAVANA_CONSTRAINTS=1 HAVOK_MOPP=1)
target_include_directories(hk_base PRIVATE
    $<TARGET_PROPERTY:ToGLESRuntime,INCLUDE_DIRECTORIES>
    ../compat
    ../../ivp/havana/havok/hk_base
    ../../ivp/havana/havok/hk_base/..
)
add_library(hk_math STATIC EXCLUDE_FROM_ALL
    ../../ivp/havana/havok/hk_math/lcp/lcp_solver.cpp
    ../../ivp/havana/havok/hk_math/incr_lu/incr_lu.cpp
    ../../ivp/havana/havok/hk_math/gauss_elimination/gauss_elimination.cpp
    ../../ivp/havana/havok/hk_math/quaternion/quaternion.cpp
    ../../ivp/havana/havok/hk_math/quaternion/quaternion_util.cpp
    ../../ivp/havana/havok/hk_math/vector3/vector3.cpp
    ../../ivp/havana/havok/hk_math/vector3/vector3_util.cpp
    ../../ivp/havana/havok/hk_math/densematrix.cpp
    ../../ivp/havana/havok/hk_math/densematrix_util.cpp
    ../../ivp/havana/havok/hk_math/eulerangles.cpp
    ../../ivp/havana/havok/hk_math/math.cpp
    ../../ivp/havana/havok/hk_math/matrix3.cpp
    ../../ivp/havana/havok/hk_math/odesolve.cpp
    ../../ivp/havana/havok/hk_math/plane.cpp
    ../../ivp/havana/havok/hk_math/rotation.cpp
    ../../ivp/havana/havok/hk_math/spatial_matrix.cpp
    ../../ivp/havana/havok/hk_math/spatial_vector.cpp
    ../../ivp/havana/havok/hk_math/transform.cpp
)
target_compile_features(hk_math PRIVATE cxx_std_11)
target_compile_definitions(hk_math PRIVATE
    $<TARGET_PROPERTY:ToGLESRuntime,COMPILE_DEFINITIONS>
    VPHYSICS_EXPORTS=1 HAVANA_CONSTRAINTS=1 HAVOK_MOPP=1)
target_include_directories(hk_math PRIVATE
    $<TARGET_PROPERTY:ToGLESRuntime,INCLUDE_DIRECTORIES>
    ../compat
    ../../ivp/havana/havok/hk_math
    ../../ivp/havana/havok/hk_math/..
    ../../ivp/havana/havok/hk_math/../../../ivp_utility
)
add_library(havana_constraints STATIC EXCLUDE_FROM_ALL
    ../../ivp/havana/havok/hk_physics/constraint/constraint.cpp
    ../../ivp/havana/havok/hk_physics/constraint/ball_socket/ball_socket_constraint.cpp
    ../../ivp/havana/havok/hk_physics/constraint/limited_ball_socket/limited_ball_socket_constraint.cpp
    ../../ivp/havana/havok/hk_physics/constraint/ragdoll/ragdoll_constraint.cpp
    ../../ivp/havana/havok/hk_physics/constraint/ragdoll/ragdoll_constraint_bp_builder.cpp
    ../../ivp/havana/havok/hk_physics/constraint/local_constraint_system/local_constraint_system.cpp
    ../../ivp/havana/havok/hk_physics/constraint/hinge/hinge_bp_builder.cpp
    ../../ivp/havana/havok/hk_physics/constraint/hinge/hinge_constraint.cpp
    ../../ivp/havana/havok/hk_physics/constraint/breakable_constraint/breakable_constraint.cpp
    ../../ivp/havana/havok/hk_physics/constraint/fixed/fixed_constraint.cpp
    ../../ivp/havana/havok/hk_physics/constraint/prismatic/prismatic_constraint.cpp
    ../../ivp/havana/havok/hk_physics/constraint/pulley/pulley_constraint.cpp
    ../../ivp/havana/havok/hk_physics/constraint/stiff_spring/stiff_spring_constraint.cpp
    ../../ivp/havana/havok/hk_physics/core/rigid_body_core.cpp
    ../../ivp/havana/havok/hk_physics/effector/rigid_body_binary_effector.cpp
)
target_compile_features(havana_constraints PRIVATE cxx_std_11)
target_compile_definitions(havana_constraints PRIVATE
    $<TARGET_PROPERTY:ToGLESRuntime,COMPILE_DEFINITIONS>
    VPHYSICS_EXPORTS=1 HAVANA_CONSTRAINTS=1 HAVOK_MOPP=1)
target_include_directories(havana_constraints PRIVATE
    $<TARGET_PROPERTY:ToGLESRuntime,INCLUDE_DIRECTORIES>
    ../compat
    ../../ivp/havana
    ../../ivp/havana/havok
    ../../ivp/havana/../ivp_physics
    ../../ivp/havana/../ivp_utility
    ../../ivp/havana/../ivp_controller
)
add_library(ivp_compactbuilder STATIC EXCLUDE_FROM_ALL
    ../../ivp/ivp_compact_builder/ivp_compact_ledge_gen.cxx
    ../../ivp/ivp_compact_builder/ivp_compact_modify.cxx
    ../../ivp/ivp_compact_builder/ivp_compact_recursive.cxx
    ../../ivp/ivp_compact_builder/ivp_convex_decompositor.cxx
    ../../ivp/ivp_compact_builder/ivp_halfspacesoup.cxx
    ../../ivp/ivp_compact_builder/ivp_i_fpoint_vhash.cxx
    ../../ivp/ivp_compact_builder/ivp_i_point_vhash.cxx
    ../../ivp/ivp_compact_builder/ivp_object_polygon_tetra.cxx
    ../../ivp/ivp_compact_builder/ivp_rot_inertia_solver.cxx
    ../../ivp/ivp_compact_builder/ivp_surbuild_halfspacesoup.cxx
    ../../ivp/ivp_compact_builder/ivp_surbuild_ledge_soup.cxx
    ../../ivp/ivp_compact_builder/ivp_surbuild_pointsoup.cxx
    ../../ivp/ivp_compact_builder/ivp_surbuild_polygon_convex.cxx
    ../../ivp/ivp_compact_builder/ivp_surbuild_polyhdrn_cncv.cxx
    ../../ivp/ivp_compact_builder/ivp_surbuild_q12.cxx
    ../../ivp/ivp_compact_builder/ivp_templates_intern.cxx
    ../../ivp/ivp_compact_builder/ivp_tetra_intrude.cxx
    ../../ivp/ivp_compact_builder/ivv_cluster_min_hash.cxx
    ../../ivp/ivp_compact_builder/qhull.cxx
    ../../ivp/ivp_compact_builder/qhull_geom.cxx
    ../../ivp/ivp_compact_builder/qhull_geom2.cxx
    ../../ivp/ivp_compact_builder/qhull_global.cxx
    ../../ivp/ivp_compact_builder/qhull_io.cxx
    ../../ivp/ivp_compact_builder/qhull_mem.cxx
    ../../ivp/ivp_compact_builder/qhull_merge.cxx
    ../../ivp/ivp_compact_builder/qhull_poly.cxx
    ../../ivp/ivp_compact_builder/qhull_poly2.cxx
    ../../ivp/ivp_compact_builder/qhull_qset.cxx
    ../../ivp/ivp_compact_builder/qhull_stat.cxx
    ../../ivp/ivp_compact_builder/qhull_user.cxx
    ../../ivp/ivp_compact_builder/geompack_cutfac.cxx
    ../../ivp/ivp_compact_builder/geompack_cvdec3.cxx
    ../../ivp/ivp_compact_builder/geompack_drdec3.cxx
    ../../ivp/ivp_compact_builder/geompack_dsphdc.cxx
    ../../ivp/ivp_compact_builder/geompack_edght.cxx
    ../../ivp/ivp_compact_builder/geompack_initcb.cxx
    ../../ivp/ivp_compact_builder/geompack_insed3.cxx
    ../../ivp/ivp_compact_builder/geompack_insfac.cxx
    ../../ivp/ivp_compact_builder/geompack_insvr3.cxx
    ../../ivp/ivp_compact_builder/geompack_prime.cxx
    ../../ivp/ivp_compact_builder/geompack_ptpolg.cxx
    ../../ivp/ivp_compact_builder/geompack_resedg.cxx
    ../../ivp/ivp_compact_builder/3dsimport_co.cxx
)
target_compile_features(ivp_compactbuilder PRIVATE cxx_std_11)
target_compile_definitions(ivp_compactbuilder PRIVATE
    $<TARGET_PROPERTY:ToGLESRuntime,COMPILE_DEFINITIONS>
    VPHYSICS_EXPORTS=1 HAVANA_CONSTRAINTS=1 HAVOK_MOPP=1)
target_include_directories(ivp_compactbuilder PRIVATE
    $<TARGET_PROPERTY:ToGLESRuntime,INCLUDE_DIRECTORIES>
    ../compat
    ../../ivp/ivp_compact_builder
    ../../ivp/ivp_compact_builder/havok
    ../../ivp/ivp_compact_builder/../havana/havok
    ../../ivp/ivp_compact_builder/../ivp_physics
    ../../ivp/ivp_compact_builder/../ivp_utility
    ../../ivp/ivp_compact_builder/../ivp_controller
    ../../ivp/ivp_compact_builder/../ivp_surface_manager
    ../../ivp/ivp_compact_builder/../ivp_collision
)
add_library(ivp_physics STATIC EXCLUDE_FROM_ALL
    ../../ivp/ivp_physics/../ivp_collision/ivp_3d_solver.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_clustering_longrange.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_clustering_lrange_hash.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_clustering_visual_hash.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_clustering_visualizer.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_coll_del_root_mindist.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_collision_filter.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_compact_ledge.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_compact_ledge_solver.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_i_collision_vhash.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_mindist.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_mindist_debug.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_mindist_event.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_mindist_mcases.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_mindist_minimize.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_mindist_recursive.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_oo_watcher.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_range_manager.cxx
    ../../ivp/ivp_physics/../ivp_collision/ivp_ray_solver.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_actuator.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_actuator_spring.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_buoyancy_solver.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_car_system.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_constraint.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_constraint_car.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_constraint_fixed_keyed.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_constraint_local.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_controller_airboat.cpp
    ../../ivp/ivp_physics/../ivp_controller/ivp_controller_buoyancy.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_controller_floating.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_controller_motion.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_controller_raycast_car.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_controller_stiff_spring.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_controller_world_frict.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_forcefield.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_multidimensional_interp.cxx
    ../../ivp/ivp_physics/../ivp_controller/ivp_template_constraint.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_ball.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_calc_next_psi_solver.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_controller_phantom.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_core.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_environment.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_friction.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_friction_gaps.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_great_matrix.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_hull_manager.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_i_friction_hash.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_i_object_vhash.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_impact.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_merge_core.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_mindist_friction.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_object.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_object_attach.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_physic.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_physic_private.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_polygon.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_sim_unit.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_solver_core_reaction.cxx
    ../../ivp/ivp_physics/../ivp_intern/ivp_time.cxx
    ../../ivp/ivp_physics/ivp_anomaly_manager.cxx
    ../../ivp/ivp_physics/ivp_betterdebugmanager.cxx
    ../../ivp/ivp_physics/ivp_betterstatisticsmanager.cxx
    ../../ivp/ivp_physics/ivp_cache_object.cxx
    ../../ivp/ivp_physics/ivp_liquid_surface_descript.cxx
    ../../ivp/ivp_physics/ivp_material.cxx
    ../../ivp/ivp_physics/ivp_performancecounter.cxx
    ../../ivp/ivp_physics/ivp_stat_manager_cback_con.cxx
    ../../ivp/ivp_physics/ivp_surface_manager.cxx
    ../../ivp/ivp_physics/ivp_templates.cxx
    ../../ivp/ivp_physics/../ivp_surface_manager/ivp_compact_surface.cxx
    ../../ivp/ivp_physics/../ivp_surface_manager/ivp_gridbuild_array.cxx
    ../../ivp/ivp_physics/../ivp_surface_manager/ivp_surman_grid.cxx
    ../../ivp/ivp_physics/../ivp_surface_manager/ivp_surman_polygon.cxx
    ../../ivp/ivp_physics/../ivp_utility/ivu_active_value.cxx
    ../../ivp/ivp_physics/../ivp_utility/ivu_bigvector.cxx
    ../../ivp/ivp_physics/../ivp_utility/ivu_geometry.cxx
    ../../ivp/ivp_physics/../ivp_utility/ivu_hash.cxx
    ../../ivp/ivp_physics/../ivp_utility/ivu_linear.cxx
    ../../ivp/ivp_physics/../ivp_utility/ivu_memory.cxx
    ../../ivp/ivp_physics/../ivp_utility/ivu_min_hash.cxx
    ../../ivp/ivp_physics/../ivp_utility/ivu_min_list.cxx
    ../../ivp/ivp_physics/../ivp_utility/ivu_os_dep.cxx
    ../../ivp/ivp_physics/../ivp_utility/ivu_quat.cxx
    ../../ivp/ivp_physics/../ivp_utility/ivu_string.cxx
    ../../ivp/ivp_physics/../ivp_utility/ivu_types.cxx
    ../../ivp/ivp_physics/../ivp_utility/ivu_vector.cxx
    ../../ivp/ivp_physics/../ivp_utility/ivu_vhash.cxx
)
target_compile_features(ivp_physics PRIVATE cxx_std_11)
target_compile_definitions(ivp_physics PRIVATE
    $<TARGET_PROPERTY:ToGLESRuntime,COMPILE_DEFINITIONS>
    VPHYSICS_EXPORTS=1 HAVANA_CONSTRAINTS=1 HAVOK_MOPP=1)
target_include_directories(ivp_physics PRIVATE
    $<TARGET_PROPERTY:ToGLESRuntime,INCLUDE_DIRECTORIES>
    ../compat
    ../../ivp/ivp_physics
    ../../ivp/ivp_physics/havok
    ../../ivp/ivp_physics/../ivp_physics
    ../../ivp/ivp_physics/../ivp_utility
    ../../ivp/ivp_physics/../ivp_controller
    ../../ivp/ivp_physics/../ivp_collision
    ../../ivp/ivp_physics/../ivp_surface_manager
    ../../ivp/ivp_physics/../ivp_intern
    ../../ivp/ivp_physics/../havana/havok
)
add_library(vphysics SHARED EXCLUDE_FROM_ALL
    ../../vphysics/convert.cpp
    ../../vphysics/../public/filesystem_helpers.cpp
    ../../vphysics/ledgewriter.cpp
    ../../vphysics/main.cpp
    ../../vphysics/physics_airboat.cpp
    ../../vphysics/physics_collide.cpp
    ../../vphysics/physics_constraint.cpp
    ../../vphysics/physics_controller_raycast_vehicle.cpp
    ../../vphysics/physics_environment.cpp
    ../../vphysics/physics_fluid.cpp
    ../../vphysics/physics_friction.cpp
    ../../vphysics/physics_material.cpp
    ../../vphysics/physics_motioncontroller.cpp
    ../../vphysics/physics_object.cpp
    ../../vphysics/physics_shadow.cpp
    ../../vphysics/physics_spring.cpp
    ../../vphysics/physics_vehicle.cpp
    ../../vphysics/physics_virtualmesh.cpp
    ../../vphysics/trace.cpp
    ../../vphysics/vcollide_parse.cpp
    ../../vphysics/vphysics_saverestore.cpp
)
target_compile_features(vphysics PRIVATE cxx_std_11)
target_compile_definitions(vphysics PRIVATE
    $<TARGET_PROPERTY:ToGLESRuntime,COMPILE_DEFINITIONS>
    VPHYSICS_EXPORTS=1 HAVANA_CONSTRAINTS=1 HAVOK_MOPP=1)
target_include_directories(vphysics PRIVATE
    $<TARGET_PROPERTY:ToGLESRuntime,INCLUDE_DIRECTORIES>
    ../compat
    ../../vphysics
    ../../vphysics/../public
    ../../vphysics/../public/tier0
    ../../vphysics/../public/tier1
    ../../vphysics/../ivp/ivp_intern
    ../../vphysics/../ivp/ivp_collision
    ../../vphysics/../ivp/ivp_physics
    ../../vphysics/../ivp/ivp_surface_manager
    ../../vphysics/../ivp/ivp_utility
    ../../vphysics/../ivp/ivp_controller
    ../../vphysics/../ivp/ivp_compact_builder
    ../../vphysics/../ivp/havana/havok
    ../../vphysics/../ivp/havana
)
engine_module(vphysics)
target_link_libraries(vphysics PRIVATE havana_constraints hk_math hk_base ivp_compactbuilder ivp_physics)
