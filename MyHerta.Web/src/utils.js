import * as THREE from "three"

// 防抖函数
export function debounce(fn, delay) {
    let timer = null
    return function (...args) {
        const context = this
        clearTimeout(timer)
        timer = setTimeout(() => {
            fn.apply(context, args)
            timer = null
        }, delay)
    }
}

// 检查鼠标是否选中了物体
export function checkRaycast(raycaster, target, mousePos, scene, camera) {
    raycaster.setFromCamera(mousePos, camera)
    const intersects = raycaster.intersectObjects(scene.children, true)

    for (let i = 0; i < intersects.length; i++) {
        let obj = intersects[i].object
        while (obj) {
            if (obj === target) 
                return true
            obj = obj.parent
        }
    }
    return false
}

/**
 * 找到模型里某几个骨骼
 * @param {GLTF} model 
 * @param  {...string} boneNames 
 * @returns {Array.<THREE.Bone>}
 */
export function findBones(model, ...boneNames) {
    const result = Array(boneNames.length).fill(null)
    
    model.scene.traverse(obj => {
        if(obj.isBone) {
            const index = boneNames.indexOf(obj.name)
            if(index >= 0)
                result[index] = obj
        }
    })
    
    return result
}

/**
 * 以键值对的形式返回模型的所有动作
 * @param {GLTF} model 
 * @param {THREE.AnimationMixer} mixer 
 * @returns {Object.<string, THREE.AnimationAction>}
 */
export function allActions(model, mixer) {
    return model.animations.reduce((obj, item) => ({ ...obj, [item.name]: mixer.clipAction(item) }), {})
}


/**
 * 将屏幕坐标转换为三维场景中指定Z值平面上的点
 * @param {THREE.Vector2} mousePos 归一化设备坐标（NDC）[-1, 1]
 * @param {number} targetZ  目标Z坐标
 * @param {THREE.Camera} camera
 * @param {THREE.Raycaster} raycaster
 * @returns {THREE.Vector3} 返回对应三维场景中的坐标
 */
export function screenPosToWorldPos(mousePos, targetZ, camera, raycaster) {    
    // 创建平面（法向量为Z轴，平面常数d = -targetZ）
    const planeNormal = new THREE.Vector3(0, 0, 1);
    const targetPlane = new THREE.Plane(planeNormal, -targetZ);
    
    raycaster.setFromCamera(mousePos, camera);
    
    // 计算射线与目标平面的交点
    const intersection = new THREE.Vector3();
    raycaster.ray.intersectPlane(targetPlane, intersection);
    
    return intersection;
}
