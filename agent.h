/**
 * Framework for NoGo and similar games (C++ 11)
 * agent.h: Define the behavior of variants of the player
 *
 * Author: Theory of Computer Games (TCG 2021)
 *         Computer Games and Intelligence (CGI) Lab, NYCU, Taiwan
 *         https://cgilab.nctu.edu.tw/
 */

#pragma once
#include <string>
#include <random>
#include <sstream>
#include <map>
#include <type_traits>
#include <algorithm>
#include "board.h"
#include "action.h"
#include <fstream>
#include <unistd.h>
#include <ctime>

// std::ostream& //debug = *(new std::ofstream);
// std::ostream& //debug = //std::cout;

class agent {
public:
	agent(const std::string& args = "") {
		std::stringstream ss("name=unknown role=unknown " + args);
		for (std::string pair; ss >> pair; ) {
			std::string key = pair.substr(0, pair.find('='));
			std::string value = pair.substr(pair.find('=') + 1);
			meta[key] = { value };
		}
	}
	virtual ~agent() {}
	virtual void open_episode(const std::string& flag = "") {}
	virtual void close_episode(const std::string& flag = "") {}
	virtual action take_action(const board& b) { return action(); }
	virtual bool check_for_win(const board& b) { return false; }

public:
	virtual std::string property(const std::string& key) const { return meta.at(key); }
	virtual void notify(const std::string& msg) { meta[msg.substr(0, msg.find('='))] = { msg.substr(msg.find('=') + 1) }; }
	virtual std::string name() const { return property("name"); }
	virtual std::string role() const { return property("role"); }

protected:
	typedef std::string key;
	struct value {
		std::string value;
		operator std::string() const { return value; }
		template<typename numeric, typename = typename std::enable_if<std::is_arithmetic<numeric>::value, numeric>::type>
		operator numeric() const { return numeric(std::stod(value)); }
	};
	std::map<key, value> meta;
};

/**
 * base agent for agents with randomness
 */
class random_agent : public agent {
public:
	random_agent(const std::string& args = "") : agent(args) {
		if (meta.find("seed") != meta.end())
			engine.seed(int(meta["seed"]));
	}
	virtual ~random_agent() {}

protected:
	std::default_random_engine engine;
};

/**
 * random player for both side
 * put a legal piece randomly
 */
class player : public random_agent {
public:
	player(const std::string& args = "") :random_agent("name=random role=unknown " + args),
		space(board::size_x * board::size_y), opp_space(board::size_x * board::size_y), who(board::empty) {
		if (name().find_first_of("[]():; ") != std::string::npos)
			throw std::invalid_argument("invalid name: " + name());
		if (role() == "black") who = board::black;
		if (role() == "white") who = board::white;
		if (who == board::empty)
			throw std::invalid_argument("invalid role: " + role());
		for (size_t i = 0; i < space.size(); i++){
			space[i] = action::place(i, who);
			//create opponet space
			if(who == board::black) opp_space[i] = action::place(i, board::white);
			if(who == board::white) opp_space[i] = action::place(i, board::black);
		}
	}
	virtual action take_action(const board& state) {
		simulation_count = stoi(property("N"));
		weight = stof(property("c"));
		timer = property("timer");
		choose = property("choose");
		node* root = new_node(state);

		if(timer=="y"){
			std::clock_t start = std::clock(); // get current time
			while(1){
				my_turn = true;
				update_nodes.push_back(root);
				insert(root,state);
				if( (std::clock()-start)/ (double) CLOCKS_PER_SEC > 1) {
					// //std::cout<<total_count<<std::endl;
					break;
				}
			}
			std::cout<<"total_count ="<<total_count<<std::endl;
		}
		else if(timer=="n"){
			while(total_count<simulation_count){
				my_turn = true;
				update_nodes.push_back(root);
				insert(root,state);
			}
		}

		total_count = 0;
		if(root->childs.size()==0) return action();

		//choose best child 
		int index = -1;
		float max=-100;
		
		if(choose=="win_rate"){
			for(size_t i = 0 ; i <root->childs.size(); i++){
				float cuurent_win_rate = root->childs[i]->win_count / root->childs[i]->visit_count ;
				if(cuurent_win_rate>max){
					max = cuurent_win_rate;
					index = i;
				}
			}
		}
		else if(choose=="visit_count"){
			for(size_t i = 0 ; i <root->childs.size(); i++){
				if(root->childs[i]->visit_count>max){
					max = root->childs[i]->visit_count;
					index = i;
				}
			}
		}
		else if(choose=="uct_value"){
			for(size_t i = 0 ; i <root->childs.size(); i++){
				if(root->childs[i]->uct_value>max){
					max = root->childs[i]->uct_value;
					index = i;
				}
			}
		}
		// //std::cout<<"root->childs.size():"<<root->childs.size()<<std::endl;
		// //std::cout<<"！！！！！！！！action index :"<<index<<std::endl;
		// //std::cout<<"ORG"<<std::endl;
		// //std::cout<<state<<std::endl;
		// //std::cout<<"It shoud be"<<std::endl;
		// //std::cout<<root->childs[index]->state<<std::endl;
		for (const action::place& move : space) {
			board after = state;
			if (move.apply(after) == board::legal){
				if(after == root->childs[index]->state){
					delete_node(root);
					return move;
				}
			}
		}
		delete_node(root);
		return action();
	}

	struct node{
		board state;
		float visit_count;
		float win_count;
		float uct_value;
		std::vector<node*> childs;
	};
	void delete_node(struct node * root){
		for(size_t i = 0 ; i<root->childs.size(); i++)
			delete_node(root->childs[i]);
		delete(root);
	}
	bool simulation(struct node * current_node){
		board after = current_node->state;
		bool end = false;
		bool win = true;
		int count = 0 ;
		// if(who == board::white) //debug<<"who == board::white"<<std::endl;
		// if(who == board::black) //debug<<"who == board::black"<<std::endl;

		if(my_turn==true) {
			//debug<<"my_turn==true"<<std::endl;
			//debug<<current_node->state<<std::endl;
			win = false;
			count = 0;
		}
		else {
			//debug<<"my_turn==false"<<std::endl;
			//debug<<current_node->state<<std::endl;
			win = true;
			count = 1;
		}
		while(!end){
			bool exist_legal_move = false;
			if(count %2 == 0 ){// my move
				std::shuffle(space.begin(), space.end(), engine);
				for (const action::place& move : space) {
					if (move.apply(after) == board::legal){
						//debug<<"count ==0 have legal move"<<std::endl;
						win = true;
						exist_legal_move = true;
						count++; 
						break;
					}
				}
			}
			else if(count %2 == 1 ) {// opponent move
				std::shuffle(opp_space.begin(), opp_space.end(), engine);
				for (const action::place& move : opp_space) {
					if (move.apply(after) == board::legal){
						//debug<<"count ==1 have legal move"<<std::endl;
						win = false;
						exist_legal_move = true;
						count++; 
						break;
					}
				}
			}
			if(!exist_legal_move) {
				//debug<<"**********************end**********************"<<std::endl;
				end = true;
			}
		}
		total_count++;
		return win;
	}

	struct node* new_node(board state){
		struct node* current_node = new struct node;
		current_node->visit_count = 0;
		current_node->win_count = 0;
		current_node->uct_value = 10000;
		current_node->state = state;
		// //std::cout<<state<<std::endl;
		return current_node;
	}

	void insert(struct node* root, board state){
		// collect child
		size_t number_of_legal_move = 0;

		if(my_turn==true){
			for (const action::place& move : space) {
				board after = state;
				if (move.apply(after) == board::legal){
					if(root->childs.size()<=number_of_legal_move++){
						struct node * current_node = new_node(after);		
						root->childs.push_back(current_node);
					}
				}
			}
		}
		else {
			for (const action::place& move : opp_space) {
				board after = state;
				if (move.apply(after) == board::legal){
					if(root->childs.size()<=number_of_legal_move++){
						struct node * current_node = new_node(after);		
						root->childs.push_back(current_node);
					}
				}
			}
		}
		// do simulation
		if(root->visit_count == 0) {
			bool win = simulation(root);
			update(win);
		}
		else {
			int index = -1;
			float max=-100;
			size_t child_visit_count = 0;
			bool do_expand = true;
			//get number of child have been visited
			for(size_t i = 0 ; i<root->childs.size(); i++) {
				if(root->childs[i]->visit_count!=0)
					child_visit_count++;
			}
			// check need expand or not
			if(child_visit_count == number_of_legal_move) do_expand = false;
			//debug<<"child_visit_count"<<child_visit_count<<", number_of_legal_move"<<number_of_legal_move<<std::endl;
			if(number_of_legal_move==0){
				bool win = simulation(root);
				update(win);
				return;
			} 

			if(do_expand){
				std::shuffle(root->childs.begin(), root->childs.end(), engine);

				for(size_t i = 0 ; i<root->childs.size(); i++){
					if(root->childs[i]->uct_value>max && root->childs[i]->visit_count==0){
						max = root->childs[i]->uct_value;
						index = i;
					}
				}
				//debug<<"expand index :"<<index<<std::endl;
			}else{
				for(size_t i = 0 ; i<root->childs.size(); i++){
					if(root->childs[i]->uct_value>max){
						max = root->childs[i]->uct_value;
						index = i;
					}
				}
				//debug<<"select index:"<<index<<std::endl;
			}
			my_turn = !my_turn;
			update_nodes.push_back(root->childs[index]);
			insert(root->childs[index],root->childs[index]->state);
		}
	}

	float UCT_value(float win_count, float visit_count){
		return  win_count/visit_count + weight * log(total_count) / visit_count ;
	}

	void update(bool win){
		float value = 0;
		if(win) value = 1;
		//debug<<"win = "<<win<<std::endl;
		for (size_t i = 0 ; i< update_nodes.size() ; i++){
			update_nodes[i]->visit_count++;
			update_nodes[i]->win_count += value;				
			if(i%2==1)
				update_nodes[i]->uct_value = UCT_value(update_nodes[i]->win_count, update_nodes[i]->visit_count);
			else
				update_nodes[i]->uct_value = UCT_value(update_nodes[i]->visit_count-update_nodes[i]->win_count, update_nodes[i]->visit_count);
		}
		// clear total_count and update_nodes
		update_nodes.clear();
	}
	float total_count = 0 ;
	std::vector<node*> update_nodes;
	bool my_turn;
private:
	int simulation_count;
	float weight;
	std::string choose;
	std::string timer;
	std::vector<action::place> space;
	std::vector<action::place> opp_space;
	board::piece_type who;
};
