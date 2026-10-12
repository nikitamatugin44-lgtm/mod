// Animation / particle speed ("a speedhack only for animations").
// This file is separate on purpose: if it does not compile, just delete it,
// the rest of the mod works without it.

#include <Geode/Geode.hpp>
#include <Geode/modify/CCParticleSystem.hpp>
#include <Geode/modify/CCActionManager.hpp>
#include "shared.hpp"

using namespace geode::prelude;

class $modify(SsbParticles, CCParticleSystem) {
	void update(float dt) {
		float k = PlayLayer::get() ? g_partFactor : 1.f;
		CCParticleSystem::update(dt * k);
	}
};

class $modify(SsbActions, CCActionManager) {
	void update(float dt) {
		float k = PlayLayer::get() ? g_animFactor : 1.f;
		CCActionManager::update(dt * k);
	}
};
