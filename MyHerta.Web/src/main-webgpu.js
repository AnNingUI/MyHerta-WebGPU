// main-webgpu.js
import { outline } from 'three/addons/tsl/display/OutlineNode.js';
import { GLTFLoader } from "three/examples/jsm/loaders/GLTFLoader";
import { randInt } from "three/src/math/MathUtils";
import { pass, uniform } from 'three/tsl';
import * as THREE from "three/webgpu";
import { WebGPURenderer } from "three/webgpu";
import "./styles.css";
import { allActions, checkRaycast, debounce, findBones, screenPosToWorldPos } from "./utils";

// 画布
const threeCanvas = document.getElementById("threeCanvas");

// 场景
const scene = new THREE.Scene();

// 灯光
const ambientLight = new THREE.AmbientLight(0xffffff, 2.8);
scene.add(ambientLight);

// 相机
const camera = new THREE.PerspectiveCamera(36, document.body.offsetWidth / document.body.offsetHeight, 0.1, 1000);
camera.position.set(0, 3.8, 12.4);

// 渲染器
// 切换为 WebGPURenderer 并进行异步初始化
const renderer = new WebGPURenderer({ antialias: true, canvas: threeCanvas });
await renderer.init();
renderer.setSize(document.body.offsetWidth, document.body.offsetHeight);
renderer.setPixelRatio(window.devicePixelRatio);

// 模型
let hertaModel = await new GLTFLoader().loadAsync("/models/herta/herta.gltf");

// 遍历模型并替换不兼容的材质
hertaModel.scene.traverse((child) => {
    if (child.isMesh && child.material) {
        const materials = Array.isArray(child.material) ? child.material : [child.material];
        materials.forEach((material, index) => {
            if (material instanceof THREE.ShaderMaterial || material instanceof THREE.RawShaderMaterial) {
                console.warn(`Replacing incompatible material: ${material.name || 'unnamed'}`);
                const newMaterial = new THREE.MeshStandardMaterial({
                    color: material.color || 0xffffff,
                    map: material.map || null,
                    transparent: material.transparent || false,
                    opacity: material.opacity || 1
                });

                if (Array.isArray(child.material)) {
                    child.material[index] = newMaterial;
                } else {
                    child.material = newMaterial;
                }
            }
        });
    }
});

hertaModel.scene.scale.multiplyScalar(5);
scene.add(hertaModel.scene);

// 需要使用的骨骼
const [headBone, neckBone] = findBones(hertaModel, "頭", "首");
const headBoneTargetQuaternion = new THREE.Quaternion();

// 动画播放器
const mixer = new THREE.AnimationMixer(hertaModel.scene);
const actions = allActions(hertaModel, mixer);
let nowActionName = "转圈圈";

actions["转圈圈_bone"].setLoop(THREE.LoopOnce);
actions["转圈圈_face"].setLoop(THREE.LoopOnce);
actions["转圈圈_bone"].clampWhenFinished = true;

mixer.addEventListener("finished", () => {
    if (nowActionName == "转圈圈")
        switchAction("站立");
});

actions[`${nowActionName}_bone`].play();
actions[`${nowActionName}_face`].play();
// WebGPU 描边效果参数
// ----------------------------------------------------------------------------------------------------
let selectedObjects = [hertaModel.scene];
const edgeStrength = uniform(3.0);
const edgeGlow = uniform(0.0);
const edgeThickness = uniform(1.0);
const visibleEdgeColor = uniform(new THREE.Color('hsl(316.17, 50%, 40%)'));
const hiddenEdgeColor = uniform(new THREE.Color('hsl(316.17, 36%, 75%)'));
const pulsePeriod = uniform(0);

// 创建描边Pass
const outlinePass = outline(scene, camera, {
    selectedObjects,
    edgeStrength,
    edgeGlow,
    edgeThickness,
    visibleEdgeColor,
    hiddenEdgeColor
});

// 反射修改私有属性
Reflect.set(outlinePass, "_visibleEdgeColor", visibleEdgeColor);
Reflect.set(outlinePass, "_hiddenEdgeColor", hiddenEdgeColor);

const scenePass = pass(scene, camera);
const postProcessing = new THREE.PostProcessing(renderer);
postProcessing.outputNode = outlinePass.add(scenePass);
// ----------------------------------------------------------------------------------------------------

// 监听：鼠标是否在黑塔上，并添加描边
let isHoveringHerta = false;
let mousePos = new THREE.Vector2();
const raycaster = new THREE.Raycaster();
let targetEdgeStrength = 0.0;
let targetEdgeGlow = 0.0;

const checkMouse = debounce(event => {
    const isHoveringHertaNow = checkRaycast(raycaster, hertaModel.scene, mousePos, scene, camera);

    if (isHoveringHertaNow != isHoveringHerta) {
        targetEdgeStrength = isHoveringHertaNow ? 3.0 : 0.0;
        targetEdgeGlow = isHoveringHertaNow ? 1.0 : 0.0;
        isHoveringHerta = isHoveringHertaNow;
        threeCanvas.className = isHoveringHertaNow ? "clickable" : "";
    }
}, 100);

// 在浏览器测试
// window.addEventListener("mousemove", event => { 
//     mousePos.x = (event.clientX / document.body.offsetWidth) * 2 - 1;
//     mousePos.y = -(event.clientY / document.body.offsetHeight) * 2 + 1;

//     checkMouse();
// });

// 切换角色动画
function switchAction(actionName, duration = 0.5) {
    if (actionName == nowActionName)
        return;

    ["bone", "face"].forEach(type => {
        const nowAction = actions[`${nowActionName}_${type}`];
        const newAction = actions[`${actionName}_${type}`];

        newAction.reset().setEffectiveWeight(1).play();
        nowAction.fadeOut(duration);
        newAction.crossFadeFrom(nowAction, duration, false);
    });

    nowActionName = actionName;
}

// 渲染循环
let clock = new THREE.Clock();
function render() {
    requestAnimationFrame(render);

    const delta = clock.getDelta();

    // 头转向鼠标方向
    if (nowActionName == "站立" || nowActionName == "坐") {
        const q = new THREE.Quaternion().copy(headBone.quaternion);
        headBone.lookAt(screenPosToWorldPos(mousePos, 7, camera, raycaster));
        headBoneTargetQuaternion.copy(headBone.quaternion);

        mixer.update(delta);

        headBone.quaternion.copy(q);
        headBone.quaternion.rotateTowards(headBoneTargetQuaternion, Math.PI * 3 / 4 * delta);
    } else {
        mixer.update(delta);
    }

    // 更新描边效果参数
    if (isHoveringHerta) {
        outlinePass.selectedObjects = selectedObjects;
        edgeStrength.value += (targetEdgeStrength - edgeStrength.value) * 0.05;
        edgeGlow.value += (targetEdgeGlow - edgeGlow.value) * 0.05;
    }
    else {
        outlinePass.selectedObjects = [];
    }

    // 使用后期处理渲染
    postProcessing.render();
}

render();

// 随机触发转圈圈
const autoSwitchActionTimer = setInterval(() => {
    if (nowActionName == "站立" && randInt(1, 30) == 1)
        switchAction("转圈圈");
}, 2000);

// 监听：窗口缩放时调整画布和渲染大小
window.addEventListener("resize", debounce(() => {
    const width = document.body.offsetWidth;
    const height = document.body.offsetHeight;

    camera.aspect = width / height;
    camera.updateProjectionMatrix();
    renderer.setSize(width, height, true);
}, 100));

// 每隔50ms与后端进行数据同步
let isSynchronizingdata = false;
const synchronizedataTimer = setInterval(() => {
    if (!isSynchronizingdata) {
        isSynchronizingdata = true;
        fetch(`/api/synchronize_data?isHoveringHerta=${isHoveringHerta ? 1 : 0}`)
            .then(res => res.json())
            .then(data => {
                if (data.hertaState == 1)
                    switchAction("坐");
                else if (data.hertaState == 0 && nowActionName == "坐")
                    switchAction("站立");

                const wndWidth = data.wndRect[2] - data.wndRect[0];
                const wndHeight = data.wndRect[3] - data.wndRect[1];
                const wndCenterX = data.wndRect[0] + wndWidth / 2;
                const wndCenterY = data.wndRect[1] + wndHeight / 2;
                mousePos = new THREE.Vector2(
                    (data.mousePos[0] - wndCenterX) * 2 / wndWidth,
                    -(data.mousePos[1] - wndCenterY) * 2 / wndHeight);
                checkMouse();
            })
            .finally(() => isSynchronizingdata = false);
    }
}, 50);