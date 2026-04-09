#ifndef ENEMY_H
#define ENEMY_H

class Enemy {
public:
    Enemy();
    virtual ~Enemy();

    void move(float deltaTime);
    void attack();
    void takeDamage(int damage);

private:
    int health;
    float speed;
    int damage;
    // Additional enemy attributes can be added here
};

#endif // ENEMY_H